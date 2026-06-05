// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct kmem_cpu {
  struct spinlock lock;
  struct run *freelist;
};

static struct kmem_cpu kmem[NCPU];
static char kmem_lock_names[NCPU][16];

struct {
  struct spinlock lock;
  int count[PHYSTOP / PGSIZE];
} ref;

static void
kmem_mkname(int id, char *buf, int sz)
{
  char tmp[8];
  int i = 0;
  int n = 0;

  if(sz <= 0)
    return;

  buf[i++] = 'k';
  if(i >= sz - 1) goto done;
  buf[i++] = 'm';
  if(i >= sz - 1) goto done;
  buf[i++] = 'e';
  if(i >= sz - 1) goto done;
  buf[i++] = 'm';
  if(i >= sz - 1) goto done;

  if(id == 0){
    buf[i++] = '0';
    goto done;
  }

  while(id > 0 && n < (int)sizeof(tmp)){
    tmp[n++] = '0' + (id % 10);
    id /= 10;
  }
  while(n > 0 && i < sz - 1)
    buf[i++] = tmp[--n];

done:
  buf[i] = 0;
}

void
kinit()
{
  initlock(&ref.lock, "kref");
  for(int i = 0; i < NCPU; i++){
    kmem_mkname(i, kmem_lock_names[i], sizeof(kmem_lock_names[i]));
    initlock(&kmem[i].lock, kmem_lock_names[i]);
  }
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE) {
    ref.count[(uint64)p / PGSIZE] = 1;
    kfree(p);
  }
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;
  int id;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  acquire(&ref.lock);
  if(ref.count[(uint64)pa / PGSIZE] <= 0)
    panic("kfree_count");
  ref.count[(uint64)pa / PGSIZE] -= 1;
  if(ref.count[(uint64)pa / PGSIZE] > 0) {
    release(&ref.lock);
    return;
  }
  release(&ref.lock);

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  push_off();
  id = cpuid();
  pop_off();

  acquire(&kmem[id].lock);
  r->next = kmem[id].freelist;
  kmem[id].freelist = r;
  release(&kmem[id].lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;
  struct run *steal_list;
  struct run *steal_tail;
  int id;
  int i;
  int count;

#define STEAL_COUNT 8

  push_off();
  id = cpuid();
  pop_off();

  acquire(&kmem[id].lock);
  r = kmem[id].freelist;
  if(r)
    kmem[id].freelist = r->next;
  release(&kmem[id].lock);

  if(r)
    goto out;

  for(i = 0; i < NCPU; i++){
    if(i == id)
      continue;

    steal_list = 0;
    steal_tail = 0;
    count = 0;

    acquire(&kmem[i].lock);
    while(kmem[i].freelist && count < STEAL_COUNT){
      struct run *n = kmem[i].freelist;
      kmem[i].freelist = n->next;
      n->next = 0;
      if(steal_tail)
        steal_tail->next = n;
      else
        steal_list = n;
      steal_tail = n;
      count++;
    }
    release(&kmem[i].lock);

    if(steal_list){
      r = steal_list;
      steal_list = steal_list->next;
      r->next = 0;

      if(steal_list){
        acquire(&kmem[id].lock);
        steal_tail->next = kmem[id].freelist;
        kmem[id].freelist = steal_list;
        release(&kmem[id].lock);
      }
      goto out;
    }
  }

out:
  if(r) {
    memset((char*)r, 5, PGSIZE); // fill with junk
    ref.count[(uint64)r / PGSIZE] = 1;
  }
  return (void*)r;
}

uint64
freemem(void)
{
  struct run *r;
  uint64 n = 0;
  int i;

  for(i = 0; i < NCPU; i++){
    acquire(&kmem[i].lock);
    for(r = kmem[i].freelist; r; r = r->next)
      n += PGSIZE;
    release(&kmem[i].lock);
  }

  return n;
}

void
kref_incr(void *pa)
{
  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kref_incr");
  acquire(&ref.lock);
  ref.count[(uint64)pa / PGSIZE] += 1;
  release(&ref.lock);
}
