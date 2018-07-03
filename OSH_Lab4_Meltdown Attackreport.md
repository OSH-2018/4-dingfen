#OSH_Lab4_Meltdown Attackreport

## 实验原理

### out of order & Memory isolation

​	从程序员的角度看，指令是按照线性的方式一个接一个运行在CPU上。然而事实上CPU在执行指令时，为了提高硬件的利用率，CPU往往是采用并行或者乱序的方式执行指令，例如，在遇到一个branch指令时，CPU不会等待分支运算的结果，而是去从别的地方调度几条与该branch指令不相干的指令，利用分支槽执行这些指令。通常，这些指令都紧挨着branch。如果分支预测正确，那么CPU就充分利用了这部分等待的时间，效率会大大提高，若出现错误，那么就必须清除分支槽的缓冲区，并初始化槽[^1]。

​	然而，在设计CPU时，Intel的设计人员在设计乱序执行时犯了一些错误。当乱序执行失败时，他们清除了寄存器中和内存中的乱序执行的影响。然而，他们忘记了指令的乱序执行会影响CPU的Cache[^2]。这是造成meltdown漏洞的一点。

​	内存隔离（memory isolation）是保障操作系统安全的基础。若内存隔离被打破，那么用户程序可以轻而易举地访问并修改内核信息，直接导致数据泄漏甚至系统崩溃。在现在流行的操作系统中，普遍的做法就是使用supervisor bits[^2]来判断程序是否有权利访问内核空间。如果一个用户空间的程序非法访问了内核空间，那么操作系统就会抛出异常（通常是段错误 SIGSEGV），并将用户程序退出。然而meltdown打破了内核与用户空间的隔离。

### side-channel attack

​	测信道攻击（side-channel attack）是计算机安全领域的一个常见的攻击手段。这个攻击手段并不是直接去打破计算机的主要的信号通路，而是利用计算机（程序）在运行过程中，产生的一些“副作用”来判断计算机内部运行的程序结果，如果测算精确，并且条件掌控合适，就可以以极大的概率获得正确的数据。

​	这次实验利用了FLUSH+RELOAD  [^3] 的测信道攻击方法。我们知道，CPU cache是CPU用来减少内存访问的平均等待时间而设计的。CPU访问cache中的数据比访问内存中的数据要快得多。当一个数据块最近被CPU访问时，它就极有可能被放入cache中，等下一次再访问它时，所消耗的时间就非常少。FLUSH+RELOAD的攻击方法主要分为3步[^3][^2]

1. FLUSH 把整个数组从cache中清除，保证在攻击开始前，数组里没有一个数据在cache中。
2. 启动”非法访问“程序。该程序会去访问数组中的一个元素。此时，它就会被放入cache中。
3. RELOAD 遍历整个数组，并且分别测量访问每个元素所需要的时间。如果其中一个元素访问时间明显快于其他元素，那么这个元素很有可能就是第二步被访问过的元素。

	#### 非法访问

		Intel设计人员在设计CPU的乱序执行时的一时疏忽，在CPU乱序执行预测失败时，没有彻底清理已经在Cache内的数据。而我们又知道，从内存中取数据比从cache中取数据要慢很多，当我们遍历整个page table时，就会发现有一些块的读取速度很明显快于其他的块。那么我们就有理由相信，这些速度比较快的块是存放在cache中的。我们可以利用一段简单的代码举一个例子。

```c
kernel_data = *(char*)kernel_data_addr;
array[kernel_data * 4096 + DELTA] += 1;
```

​	在此例中，第一行代码通过”非法访问“内核中的数据kernel_data，这时候，会触发一个异常，程序会跳转到异常处理程序，此时，虽然程序已经触发了异常，但是还没有将异常结果跑出来，由于CPU的乱序执行设计，CPU不会等待异常程序的处理结果，而是直接往下继续处理指令，即用kernel_data去当作一个块的编号访存，然后kernel_data号块会从主存中调入cache。假设调入到cache后，异常处理程序执行完毕，提出段错误，将此次非法访问取消。但由于CPU设计人员的疏忽，没有将这个块重新调回到主存中，因此利用测信道攻击（side-channel attack），可以轻松的找到对应的块的编号，这个编号也就是kernel_data。

### Meltdown

​	总的来说，meltdown就是利用CPU的乱序执行产生的漏洞偷窃我们的内核数据的。然而在编码开始之前，还是需要了解一些基本知识。

 - 测量访问每个元素所需要的时间。在编写攻击程序时，测量访问元素的时间必须十分精确，使用time()函数是不合适的。使用__rdtscp()函数可以更加精确。\_\_rdtscp()函数返回一个64位的时间戳计数结果。

   ```c
   time1 = __rdtscp(&junk);
   junk = *addr;
   time2 = __rdtscp(&junk) - time1;
   ```

   上述代码就可以测得访问addr所需的tick count。当测量值小于一个阈值时，认为addr存于cache中。

 - 为了将攻击目标明确，并得到一定的效果，我将一串secret字符串存在内核空间中。具体做法是，用一个内核模块来存储这个secret字符串。这部分的代码实现引自[2]。在插入内核模块后，在攻击时，需要知道secret字符串存储的具体位置，这样在用meltdown攻击时，才能得到预想的效果。

 - 前文提到，当程序非法访问一个地址时，操作系统会抛出一个异常，然后结束该进程。我当然不希望我的攻击进程因此被操作系统结束。所以，还需要自己写一个异常处理程序，类似于python中的try/except语句，在抛出异常后，程序仍能正常运行。然而，与C++和其他高级语言不同的是，C语言中没有提供支持异常处理的语句[^2]。因此，只能使用sigsetjmp()和siglongjmp()来模拟try语句的实现[^2]。

 - 异常处理机制的实现[^2]。

   1. 建立signal handler。当抛出SIGSEGV异常时，处理程序catch_segv()会被调用。`signal(SIGSEGV,catch_segv);`
   2. 建立checkpoint。当signal handler处理完异常后，进程需要从checkpoint开始继续执行。因此，在跳转前，需要将上下文环境保存起来，sigsetjmp()函数就是完成这样的一个工作。`sigsetjmp(jbuf,1);`   当返回值为0时，checkpoint被成功建立。
   3. 跳回到checkpoint。当程序抛出异常时，处理程序会被执行，执行完毕后，还要跳回到现场继续往下执行。第2点使用了sigsetjmp()函数保存了现场，相应的，需要用siglongjmp()函数恢复现场。

- 汇编代码。在CPU访存阶段，如果需要的数据不在cache中，那么CPU就需要从内存中调用数据。在这段时间里，CPU的算术执行模块会去执行其他的指令。为了提高实验的成功率，可以编写一段汇编码，让算术执行模块有事可干。代码引自[2]。在这段汇编代码中，`.rept 400;`让整个程序段循环400次，在循环内部，`add $0x141, %%eax;` 把0x141加到eax寄存器中，这段代码没有什么特殊的意义，只是为了让CPU的运算模块有事可干。

  ```c
  void meltdown_asm(unsigned long kernel_data_addr)
  {
  char kernel_data = 0;
  // Give eax register something to do
  asm volatile(
  ".rept 1000;"
  "add $0x141, %%eax;"
  ".endr;"
  :
  :
  : "eax"
  );
  ```

 ## 实验环境

内核：Linux  4.13.0-43-generic  操作系统 16.04.1-Ubuntu  体系架构 SMP  x86_64 GNU/Linux

补丁情况：打开/etc/default/grub，在GRUB_CMDLINE_LINUX_DEFAULT加入nopti

##实验过程——攻击步骤

1. 在该实验开始之前，我的机器上已经安装了防meltdown漏洞的系统补丁。因此，本次实验第一步，就是要修改内核的启动参数，关闭该补丁的功能。打开/etc/defaut/grub，在GRUB_CMDLINE_LINUX_DEFAULT加入nopti即可。

2. 关闭补丁功能后，编写内核模块，编译后再插入内核模块，并找到内核模块插入具体地址。内核模块源码为MeltdownKernel.c。具体步骤为[^2]

   ```
   $ make
   $ sudo insmod MeltdownKernel.ko
   $ dmesg | grep ’secret data address’
   secret data address: 0xffffffffc0b7b000
   ```

3. 编写攻击程序。源码为MeltdownAttack.c。然后编译运行

   ```
   $ gcc -march=native MeltdownAttack.c -o MeltdownAttack.o -O0
   $ ./MeltdownAttack.o
   ```

4. 如果攻击成功，就会显示出存放在内核模块的secret字符串。

##实验结果

## 参考文献

[1] Meltdown Moritz Lipp , Michael Schwarz, Daniel Gruss, Thomas Prescher , Werner Haas ,Stefan Mangard, Paul Kocher, Daniel Genkin , Yuval Yarom, Mike Hamburg,Graz University of Technology Cyberus Technology GmbH Independent University of Pennsylvania and University of Maryland University of Adelaide and Data Rambus, Cryptography Research Division

[2] Meltdown Attack Lab  2018 Wenliang Du, Syracuse University.

[3] Yuval Yarom and Katrina Falkner. Flush+reload: A high resolution, low noise, l3 cache side-channel
attack. In Proceedings of the 23rd USENIX Conference on Security Symposium, SEC’14, pages 719–
732, Berkeley, CA, USA, 2014. USENIX Association.