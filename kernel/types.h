typedef unsigned int   uint;
typedef unsigned short ushort;
typedef unsigned char  uchar;

typedef unsigned char uint8;
typedef unsigned short uint16;
typedef unsigned int  uint32;
typedef unsigned long uint64;

typedef uint64 pde_t;

// Claude AI was used and implemented in project 4
// Per-physical-frame metadata node for the LRU list (lru.c).
// Uses uint64* instead of pagetable_t to avoid a forward-reference issue.
struct page {
  struct page  *next;
  struct page  *prev;
  uint64       *pagetable; // pagetable_t; 0 means not on the LRU list
  uint64        vaddr;     // user virtual address this frame is mapped to
};
