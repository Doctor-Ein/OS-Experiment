#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  if(argc != 2){
    fprintf(2, "usage: sleep ticks\n");
    exit(1);
  }

  int ticks = atoi(argv[1]);
  if(ticks < 0){
    fprintf(2, "sleep: ticks must be >= 0\n");
    exit(1);
  }

  // 这里调用的是 user/user.h 里的 sleep(int)
  if(sleep(ticks) < 0){
    // 进程在睡眠期间若被 kill，内核 sys_sleep 会返回 -1。
    fprintf(2, "sleep: interrupted\n");
    exit(1);
  }

  exit(0);
}