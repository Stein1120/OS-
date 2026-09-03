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

// A prime number of buckets spreads sequential disk blocks well.
#define NBUCKET 31

struct bucket {
  struct spinlock lock;
  struct buf head;
};

struct {
  struct spinlock evict_lock;
  struct bucket bucket[NBUCKET];
  struct buf buf[NBUF];
} bcache;

static uint
bhash(uint dev, uint blockno)
{
  return (dev ^ blockno) % NBUCKET;
}

static void
binsert(struct bucket *bucket, struct buf *b)
{
  b->next = bucket->head.next;
  b->prev = &bucket->head;
  bucket->head.next->prev = b;
  bucket->head.next = b;
}

static void
bremove(struct buf *b)
{
  b->next->prev = b->prev;
  b->prev->next = b->next;
}

void
binit(void)
{
  struct buf *b;

  initlock(&bcache.evict_lock, "bcache.evict");

  for(int i = 0; i < NBUCKET; i++){
    initlock(&bcache.bucket[i].lock, "bcache.bucket");
    bcache.bucket[i].head.prev = &bcache.bucket[i].head;
    bcache.bucket[i].head.next = &bcache.bucket[i].head;
  }

  // Distribute initially unused buffers across all buckets.
  for(int i = 0; i < NBUF; i++){
    b = &bcache.buf[i];
    b->dev = 0;
    b->blockno = i;
    initsleeplock(&b->lock, "buffer");
    binsert(&bcache.bucket[i % NBUCKET], b);
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;
  uint h = bhash(dev, blockno);
  struct bucket *bucket = &bcache.bucket[h];

  acquire(&bucket->lock);
  for(b = bucket->head.next; b != &bucket->head; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bucket->lock);
      acquiresleep(&b->lock);
      return b;
    }
  }
  release(&bucket->lock);

  // Serialize only the uncommon miss/eviction path.  Recheck after
  // taking this lock so two misses cannot create duplicate copies.
  acquire(&bcache.evict_lock);
  acquire(&bucket->lock);
  for(b = bucket->head.next; b != &bucket->head; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bucket->lock);
      release(&bcache.evict_lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // Prefer an unused buffer already in the destination bucket.
  for(b = bucket->head.next; b != &bucket->head; b = b->next){
    if(b->refcnt == 0){
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
      release(&bucket->lock);
      release(&bcache.evict_lock);
      acquiresleep(&b->lock);
      return b;
    }
  }
  release(&bucket->lock);

  // The destination bucket is full.  Reserve and detach any unused
  // buffer from another bucket, then rehash it into this one.
  for(uint i = 0; i < NBUCKET; i++){
    if(i == h)
      continue;

    struct bucket *victim = &bcache.bucket[i];
    acquire(&victim->lock);
    for(b = victim->head.next; b != &victim->head; b = b->next){
      if(b->refcnt == 0){
        b->refcnt = 1;
        bremove(b);
        release(&victim->lock);

        acquire(&bucket->lock);
        b->dev = dev;
        b->blockno = blockno;
        b->valid = 0;
        binsert(bucket, b);
        release(&bucket->lock);
        release(&bcache.evict_lock);

        acquiresleep(&b->lock);
        return b;
      }
    }
    release(&victim->lock);
  }

  release(&bcache.evict_lock);
  panic("bget: no buffers");
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

  uint h = bhash(b->dev, b->blockno);
  acquire(&bcache.bucket[h].lock);
  b->refcnt--;
  release(&bcache.bucket[h].lock);
}

void
bpin(struct buf *b) {
  uint h = bhash(b->dev, b->blockno);
  acquire(&bcache.bucket[h].lock);
  b->refcnt++;
  release(&bcache.bucket[h].lock);
}

void
bunpin(struct buf *b) {
  uint h = bhash(b->dev, b->blockno);
  acquire(&bcache.bucket[h].lock);
  b->refcnt--;
  release(&bcache.bucket[h].lock);
}
