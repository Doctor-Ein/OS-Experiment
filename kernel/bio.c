// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

#define NBUCKET 13

struct bucket {
  struct spinlock lock;
  struct buf *list;
};

struct {
  struct bucket buckets[NBUCKET];
  struct spinlock evict_lock;
  struct buf buf[NBUF];
} bcache;

static uint
bucket_id(uint blockno)
{
  return blockno % NBUCKET;
}

static void
bucket_insert(struct bucket *bucket, struct buf *b)
{
  b->next = bucket->list;
  b->prev = 0;
  if(bucket->list)
    bucket->list->prev = b;
  bucket->list = b;
}

static void
bucket_remove(struct bucket *bucket, struct buf *b)
{
  if(b->prev)
    b->prev->next = b->next;
  else
    bucket->list = b->next;
  if(b->next)
    b->next->prev = b->prev;
  b->next = 0;
  b->prev = 0;
}

void
binit(void)
{
  struct buf *b;
  int i;

  for(i = 0; i < NBUCKET; i++){
    initlock(&bcache.buckets[i].lock, "bcache.bucket");
    bcache.buckets[i].list = 0;
  }
  initlock(&bcache.evict_lock, "bcache.evict");

  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    initsleeplock(&b->lock, "buffer");
    b->refcnt = 0;
    b->valid = 0;
    b->dev = 0;
    b->blockno = 0;
    bucket_insert(&bcache.buckets[0], b);
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;
  struct buf *freebuf;
  uint target;
  uint free_bucket;
  int i;

  target = bucket_id(blockno);

  acquire(&bcache.buckets[target].lock);
  for(b = bcache.buckets[target].list; b; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.buckets[target].lock);
      acquiresleep(&b->lock);
      return b;
    }
  }
  release(&bcache.buckets[target].lock);

  while(1){
    freebuf = 0;
    free_bucket = 0;
    acquire(&bcache.evict_lock);
    for(i = 0; i < NBUCKET; i++){
      acquire(&bcache.buckets[i].lock);
      for(b = bcache.buckets[i].list; b; b = b->next){
        if(b->refcnt == 0){
          freebuf = b;
          free_bucket = i;
          bucket_remove(&bcache.buckets[i], b);
          b->refcnt = 1;
          release(&bcache.buckets[i].lock);
          release(&bcache.evict_lock);
          goto have_free;
        }
      }
      release(&bcache.buckets[i].lock);
    }
    release(&bcache.evict_lock);
    panic("bget: no buffers");

have_free:
    acquire(&bcache.buckets[target].lock);
    for(b = bcache.buckets[target].list; b; b = b->next){
      if(b->dev == dev && b->blockno == blockno){
        b->refcnt++;
        release(&bcache.buckets[target].lock);

        acquire(&bcache.buckets[free_bucket].lock);
        freebuf->refcnt = 0;
        bucket_insert(&bcache.buckets[free_bucket], freebuf);
        release(&bcache.buckets[free_bucket].lock);

        acquiresleep(&b->lock);
        return b;
      }
    }

    freebuf->dev = dev;
    freebuf->blockno = blockno;
    freebuf->valid = 0;
    bucket_insert(&bcache.buckets[target], freebuf);
    release(&bcache.buckets[target].lock);
    acquiresleep(&freebuf->lock);
    return freebuf;
  }
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  acquire(&bcache.buckets[bucket_id(b->blockno)].lock);
  b->refcnt--;
  release(&bcache.buckets[bucket_id(b->blockno)].lock);
}

void
bpin(struct buf *b) {
  acquire(&bcache.buckets[bucket_id(b->blockno)].lock);
  b->refcnt++;
  release(&bcache.buckets[bucket_id(b->blockno)].lock);
}

void
bunpin(struct buf *b) {
  acquire(&bcache.buckets[bucket_id(b->blockno)].lock);
  b->refcnt--;
  release(&bcache.buckets[bucket_id(b->blockno)].lock);
}


