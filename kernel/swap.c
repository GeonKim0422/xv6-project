// PA4 (Project 4) - Page Replacement: swap.c
// Claude AI was used and implemented in project 4
//
// Provided helpers (swapinit, swapread, swapwrite, swapstat,
//   swap_alloc_slot, swap_free_slot) are already implemented.
// swap_out and swap_in are the student-implemented functions.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

#define BLKS_PER_PAGE   (PGSIZE / BSIZE)             // 4
#define NSWAPSLOTS      (SWAPMAX / BLKS_PER_PAGE)    // total page-sized slots

// I/O statistics (disk blocks, not pages).
struct {
  struct spinlock lock;
  int nr_sectors_read;
  int nr_sectors_write;
} swapstats;

// Swap-slot bitmap.  bitmap[i] == 1  =>  slot i is occupied.
struct {
  struct spinlock lock;
  uchar          *bits;
  uint            next;
} swapmap;

void
swapinit(void)
{
  initlock(&swapstats.lock, "swapstats");
  swapstats.nr_sectors_read  = 0;
  swapstats.nr_sectors_write = 0;

  initlock(&swapmap.lock, "swapmap");
  swapmap.bits = (uchar *)kalloc();
  if(swapmap.bits == 0)
    panic("swapinit: kalloc bitmap");
  memset(swapmap.bits, 0, PGSIZE);
  swapmap.next = 0;
}

// Allocate a free swap slot.  Returns -1 if the swap is full.
int
swap_alloc_slot(void)
{
  acquire(&swapmap.lock);
  for(uint tries = 0; tries < NSWAPSLOTS; tries++){
    uint i = (swapmap.next + tries) % NSWAPSLOTS;
    uint byte = i >> 3;
    uchar mask = 1 << (i & 7);
    if((swapmap.bits[byte] & mask) == 0){
      swapmap.bits[byte] |= mask;
      swapmap.next = (i + 1) % NSWAPSLOTS;
      release(&swapmap.lock);
      return (int)i;
    }
  }
  release(&swapmap.lock);
  return -1;
}

// Mark a swap slot as free.
void
swap_free_slot(uint slot)
{
  if(slot >= NSWAPSLOTS)
    panic("swap_free_slot: bad slot");
  acquire(&swapmap.lock);
  uint byte = slot >> 3;
  uchar mask = 1 << (slot & 7);
  if((swapmap.bits[byte] & mask) == 0){
    release(&swapmap.lock);
    panic("swap_free_slot: double-free");
  }
  swapmap.bits[byte] &= ~mask;
  release(&swapmap.lock);
}

// Read one page from swap slot blkno into the physical page at ptr.
void
swapread(uint64 ptr, int blkno)
{
  if(blkno < 0 || blkno >= (int)NSWAPSLOTS)
    panic("swapread: bad slot");

  uint disk_blk = SWAPBASE + (uint)blkno * BLKS_PER_PAGE;
  char *dst = (char *)ptr;

  for(int i = 0; i < BLKS_PER_PAGE; i++){
    struct buf *b = bread(ROOTDEV, disk_blk + i);
    memmove(dst + i * BSIZE, b->data, BSIZE);
    brelse(b);
  }

  acquire(&swapstats.lock);
  swapstats.nr_sectors_read += BLKS_PER_PAGE;
  release(&swapstats.lock);
}

// Write one page from ptr into swap slot blkno.
void
swapwrite(uint64 ptr, int blkno)
{
  if(blkno < 0 || blkno >= (int)NSWAPSLOTS)
    panic("swapwrite: bad slot");

  uint disk_blk = SWAPBASE + (uint)blkno * BLKS_PER_PAGE;
  char *src = (char *)ptr;

  for(int i = 0; i < BLKS_PER_PAGE; i++){
    struct buf *b = bread(ROOTDEV, disk_blk + i);
    memmove(b->data, src + i * BSIZE, BSIZE);
    bwrite(b);
    brelse(b);
  }

  acquire(&swapstats.lock);
  swapstats.nr_sectors_write += BLKS_PER_PAGE;
  release(&swapstats.lock);
}

// Expose running I/O counters (kernel pointers; sys_swapstat does copyout).
void
swapstat(int *nr_sectors_read, int *nr_sectors_write)
{
  acquire(&swapstats.lock);
  if(nr_sectors_read)  *nr_sectors_read  = swapstats.nr_sectors_read;
  if(nr_sectors_write) *nr_sectors_write = swapstats.nr_sectors_write;
  release(&swapstats.lock);
}

// ============================================================================
// swap_out:
//   Free a physical frame by evicting one of the user-mapped pages via the
//   clock algorithm.  Returns the freed frame address, or 0 on failure.
//
// Steps:
//   1. Ask the LRU subsystem for a victim frame.
//   2. Reserve a swap slot.  If full, put the victim back and return 0.
//   3. Write the victim's contents to disk.
//   4. Rewrite the victim's PTE:
//        - PPN  <- swap-slot index (encoded via SLOT2PTE)
//        - clear PTE_V  (not resident)
//        - set   PTE_S  (swapped out)
//        - preserve PTE_U / PTE_R / PTE_W / PTE_X
//   5. Flush the TLB.
//   6. Return the freed frame so kalloc() can hand it to the next request.
// ============================================================================
void *
swap_out(void)
{
  pagetable_t pt;
  uint64      va;

  // 1. Select a victim via the clock algorithm.
  uint64 pa = lru_select_victim(&pt, &va);
  if(pa == 0){
    // LRU list is empty; cannot evict anything.
    printf("kalloc: out of memory\n");
    return 0;
  }

  // 2. Reserve a free swap slot.
  int slot = swap_alloc_slot();
  if(slot < 0){
    // Swap area is full; put the victim back and give up.
    lru_add(pt, va, pa);
    printf("kalloc: out of memory\n");
    return 0;
  }

  // 3. Write the victim page to disk.
  swapwrite(pa, slot);

  // 4. Rewrite the owning PTE.
  pte_t *pte = walk(pt, va, 0);
  if(pte == 0)
    panic("swap_out: walk failed");

  // Keep permission bits; replace physical address with slot index.
  pte_t flags = PTE_FLAGS(*pte) & ~PTE_V;   // clear Valid
  flags |= PTE_S;                             // mark Swapped-out
  *pte = SLOT2PTE(slot) | flags;

  // 5. Flush stale TLB entries so the MMU will fault on next access.
  sfence_vma();

  // 6. Return the freed physical frame.
  return (void *)pa;
}

// ============================================================================
// swap_in:
//   Bring a swapped-out page back into memory.  Called from usertrap() when
//   a page fault hits a PTE with PTE_V == 0 and PTE_S == 1.
//
// Returns  0 on success (the faulting instruction will re-execute).
// Returns -1 if this is not a swap fault or if memory is exhausted.
// ============================================================================
int
swap_in(pagetable_t pt, uint64 va)
{
  // 1. Align to page boundary.
  va = PGROUNDDOWN(va);

  // 2. Walk the page table and verify it is a swapped-out entry.
  pte_t *pte = walk(pt, va, 0);
  if(pte == 0)
    return -1;
  // Must have PTE_S set and PTE_V clear to be a valid swap entry.
  if((*pte & PTE_S) == 0 || (*pte & PTE_V) != 0)
    return -1;

  // 3. Extract the swap slot index.
  uint slot = PTE2SLOT(*pte);

  // 4. Allocate a fresh physical frame.
  //    Note: kalloc() may recursively call swap_out() -- that's fine by
  //    design, but we must not let it evict the very page we're swapping in.
  //    Because we cleared PTE_V above (and it was already 0), the clock
  //    algorithm will skip this PTE (stale node check in lru_select_victim),
  //    so there is no danger of a self-eviction.
  char *mem = kalloc();
  if(mem == 0)
    return -1;

  // 5. Read the page from disk and free the swap slot.
  swapread((uint64)mem, (int)slot);
  swap_free_slot(slot);

  // 6. Restore the PTE: new frame address, PTE_V = 1, PTE_S = 0.
  pte_t flags = PTE_FLAGS(*pte) & ~PTE_S;   // clear Swapped-out
  flags |= PTE_V;                             // set Valid
  *pte = PA2PTE((uint64)mem) | flags;

  // 7. Track the new frame in the LRU list.
  lru_add(pt, va, (uint64)mem);

  // 8. Flush the TLB so the new mapping is visible immediately.
  sfence_vma();

  return 0;
}
