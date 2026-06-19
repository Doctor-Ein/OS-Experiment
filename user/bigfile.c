#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fcntl.h"
#include "kernel/fs.h"

int
main()
{
  enum { CHUNK = 3 };
  char buf[CHUNK * BSIZE];
  int fd, i, j, blocks;

  fd = open("big.file", O_CREATE | O_WRONLY);
  if(fd < 0){
    printf("bigfile: cannot open big.file for writing\n");
    exit(-1);
  }

  blocks = 0;
  while(1){
    for(i = 0; i < CHUNK; i++)
      *(int*)(buf + i * BSIZE) = blocks + i;
    int cc = write(fd, buf, sizeof(buf));
    if(cc <= 0){
      cc = write(fd, buf, BSIZE);
      if(cc <= 0)
        break;
    }
    blocks += cc / BSIZE;
    if (blocks % 1000 == 0)
      printf(".");
  }

  printf("\nwrote %d blocks\n", blocks);
  if(blocks != 65803) {
    printf("bigfile: file is too small\n");
    exit(-1);
  }
  
  close(fd);
  fd = open("big.file", O_RDONLY);
  if(fd < 0){
    printf("bigfile: cannot re-open big.file for reading\n");
    exit(-1);
  }
  for(i = 0; i < blocks; i++){
    int nblocks = blocks - i;
    if(nblocks > CHUNK)
      nblocks = CHUNK;
    int cc = read(fd, buf, nblocks * BSIZE);
    if(cc <= 0){
      printf("bigfile: read error at block %d\n", i);
      exit(-1);
    }
    for(j = 0; j < cc / BSIZE; j++){
      if(*(int*)(buf + j * BSIZE) != i + j){
        printf("bigfile: read the wrong data (%d) for block %d\n",
               *(int*)(buf + j * BSIZE), i + j);
        exit(-1);
      }
    }
    i += cc / BSIZE - 1;
  }

  printf("bigfile done; ok\n"); 

  exit(0);
}
