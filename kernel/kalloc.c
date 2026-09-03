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

void
kinit()
{
  for(int i = 0; i < NCPU; i++)
    initlock(&kmem[i].lock, "kmem.cpu");
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  // A process can migrate between CPUs if interrupts are enabled, so
  // keep them disabled from reading cpuid() through updating its list.
  push_off();
  int id = cpuid();
  acquire(&kmem[id].lock);
  r->next = kmem[id].freelist;
  kmem[id].freelist = r;
  release(&kmem[id].lock);
  pop_off();
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  push_off();
  int id = cpuid();

  acquire(&kmem[id].lock);
  r = kmem[id].freelist;
  if(r)
    kmem[id].freelist = r->next;
  release(&kmem[id].lock);

  // kinit() puts all pages on CPU 0's list.  When a local list is
  // empty, steal a batch so future allocations remain local.
  if(r == 0){
    for(int off = 1; off < NCPU; off++){
      int victim = (id + off) % NCPU;
      struct run *batch;
      struct run *tail;

      acquire(&kmem[victim].lock);
      batch = kmem[victim].freelist;
      if(batch){
        tail = batch;
        for(int n = 1; n < 64 && tail->next; n++)
          tail = tail->next;
        kmem[victim].freelist = tail->next;
        tail->next = 0;
      }
      release(&kmem[victim].lock);

      if(batch){
        r = batch;
        batch = batch->next;
        r->next = 0;

        if(batch){
          acquire(&kmem[id].lock);
          tail->next = kmem[id].freelist;
          kmem[id].freelist = batch;
          release(&kmem[id].lock);
        }
        break;
      }
    }
  }

  pop_off();

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
