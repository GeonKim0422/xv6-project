// PA4 (Project 4) - LRU list of swappable user pages.
// Claude AI was used and implemented in project 4
//
// Circular doubly-linked list of user-mapped physical frames.
// lru.head = oldest (first victim candidate).
// Clock algorithm: PTE_A==1 → clear & move to tail; PTE_A==0 → evict.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

#define NPHYS_PAGES ((PHYSTOP - KERNBASE) / PGSIZE)
struct page pages[NPHYS_PAGES];

struct {
  struct spinlock lock;
  struct page    *head;
  int             count;
} lru;

static inline struct page *
pa_to_page(uint64 pa)
{
  if(pa < KERNBASE || pa >= PHYSTOP)
    panic("pa_to_page: bad pa");
  return &pages[(pa - KERNBASE) / PGSIZE];
}

void
lruinit(void)
{
  initlock(&lru.lock, "lru");
  lru.head  = 0;
  lru.count = 0;
  for(int i = 0; i < NPHYS_PAGES; i++){
    pages[i].next      = 0;
    pages[i].prev      = 0;
    pages[i].pagetable = 0;
    pages[i].vaddr     = 0;
  }
}

int
lru_size(void)
{
  int n;
  acquire(&lru.lock);
  n = lru.count;
  release(&lru.lock);
  return n;
}

// Internal: unlink pg. Caller must hold lru.lock.
static void
lru_unlink(struct page *pg)
{
  if(lru.count == 1){
    lru.head = 0;
  } else {
    pg->prev->next = pg->next;
    pg->next->prev = pg->prev;
    if(lru.head == pg)
      lru.head = pg->next;
  }
  pg->next = 0;
  pg->prev = 0;
  lru.count--;
}

// Internal: insert pg at tail. Caller must hold lru.lock.
static void
lru_insert_tail(struct page *pg)
{
  if(lru.head == 0){
    pg->next = pg;
    pg->prev = pg;
    lru.head = pg;
  } else {
    struct page *tail = lru.head->prev;
    tail->next     = pg;
    pg->prev       = tail;
    pg->next       = lru.head;
    lru.head->prev = pg;
  }
  lru.count++;
}

void
lru_add(pagetable_t pt, uint64 va, uint64 pa)
{
  struct page *pg = pa_to_page(pa);
  acquire(&lru.lock);
  if(pg->pagetable != 0)
    lru_unlink(pg);
  pg->pagetable = pt;
  pg->vaddr     = va;
  lru_insert_tail(pg);
  release(&lru.lock);
}

void
lru_remove(uint64 pa)
{
  struct page *pg = pa_to_page(pa);
  acquire(&lru.lock);
  if(pg->pagetable == 0){
    release(&lru.lock);
    return;
  }
  lru_unlink(pg);
  pg->pagetable = 0;
  pg->vaddr     = 0;
  release(&lru.lock);
}

// Clock algorithm: walk the LRU list and pick a victim.
// Key design: we hold lru.lock only briefly to read/modify list pointers.
// walk() and sfence_vma() are called WITHOUT holding lru.lock to avoid
// any sleep-with-spinlock-held scenario.
uint64
lru_select_victim(pagetable_t *out_pt, uint64 *out_va)
{
  // We do up to 2 * count iterations total.
  // Each iteration we peek at lru.head, drop the lock, walk the PTE,
  // re-acquire and act on what we found.
  for(int iter = 0; iter < 2048 * 2; iter++){
    acquire(&lru.lock);

    if(lru.head == 0){
      release(&lru.lock);
      return 0;
    }

    // Snapshot head's info under the lock.
    struct page *pg = lru.head;
    pagetable_t  pt = (pagetable_t)pg->pagetable;
    uint64       va = pg->vaddr;

    // Release lock before walk() to avoid any recursive lock issues.
    release(&lru.lock);

    // Walk the page table (no allocation, no sleep).
    pte_t *pte = walk(pt, va, 0);

    // Re-acquire to act on the result.
    acquire(&lru.lock);

    // If head changed while we dropped the lock, try again.
    if(lru.head != pg || pg->pagetable != pt || pg->vaddr != va){
      release(&lru.lock);
      continue;
    }

    // Stale entry: PTE gone or not valid and not swapped.
    if(pte == 0 || ((*pte & PTE_V) == 0 && (*pte & PTE_S) == 0)){
      lru_unlink(pg);
      pg->pagetable = 0;
      pg->vaddr     = 0;
      release(&lru.lock);
      continue;
    }

    // Already swapped out somehow - skip.
    if((*pte & PTE_V) == 0){
      // Rotate to next.
      lru.head = pg->next;
      release(&lru.lock);
      continue;
    }

    if(*pte & PTE_A){
      // Recently accessed: clear PTE_A, rotate to tail (second chance).
      *pte &= ~PTE_A;
      // Move head forward (rotate: give this page another chance).
      lru.head = pg->next;
      release(&lru.lock);
      sfence_vma();
    } else {
      // Victim found: PTE_A == 0.
      uint64 pa = KERNBASE + (uint64)(pg - pages) * PGSIZE;
      *out_pt = (pagetable_t)pg->pagetable;
      *out_va = pg->vaddr;
      lru_unlink(pg);
      pg->pagetable = 0;
      pg->vaddr     = 0;
      release(&lru.lock);
      return pa;
    }
  }

  // Could not find a victim after many iterations.
  return 0;
}
