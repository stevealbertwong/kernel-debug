#ifndef THREADS_PTE_H
#define THREADS_PTE_H

#include "threads/vaddr.h"

/* Functions and macros for working with x86 hardware page tables.

   Virtual addresses are structured as follows:

    31                  22 21                  12 11                   0
   +----------------------+----------------------+----------------------+
   | Page Directory Index |   Page Table Index   |    Page Offset       |
   +----------------------+----------------------+----------------------+
*/

/* Page table index (bits 12:21). */
#define	PTSHIFT PGBITS		           /* First page table bit. */
#define PTBITS  10                         /* Number of page table bits. */
#define PTSPAN  (1 << PTBITS << PGBITS)    /* Bytes covered by a page table. */
#define PTMASK  BITMASK(PTSHIFT, PTBITS)   /* Page table bits (12:21). */

/* Page directory index (bits 22:31). */
#define PDSHIFT (PTSHIFT + PTBITS)         /* First page directory bit. */
#define PDBITS  10                         /* Number of page dir bits. */
#define PDMASK  BITMASK(PDSHIFT, PDBITS)   /* Page directory bits (22:31). */

static inline unsigned pt_no (const void *va) {
  return ((uintptr_t) va & PTMASK) >> PTSHIFT;
}

static inline uintptr_t pd_no (const void *va) {
  return (uintptr_t) va >> PDSHIFT;
}

/* Page directory and page table entries.

   For more information see the section on page tables in the
   Pintos reference guide chapter, or [IA32-v3a] 3.7.6
   "Page-Directory and Page-Table Entries".

   PDEs and PTEs share a common format:

   31                                 12 11                     0
   +------------------------------------+------------------------+
   |         Physical Address           |         Flags          |
   +------------------------------------+------------------------+

   In a PDE, the physical address points to a page table.
   In a PTE, the physical address points to a data or code page.
   The important flags are listed below.
   When a PDE or PTE is not "present", the other flags are
   ignored.
   A PDE or PTE that is initialized to 0 will be interpreted as
   "not present", which is just fine. */
#define PTE_FLAGS 0x00000fff    /* Flag bits. */
#define PTE_ADDR  0xfffff000    /* Address bits. */
#define PTE_AVL   0x00000e00    /* Bits available for OS use. */
#define PTE_P 0x1               /* 1=present, 0=not present. */
#define PTE_W 0x2               /* 1=read/write, 0=read-only. */
#define PTE_U 0x4               /* 1=user/kernel, 0=kernel only. */
#define PTE_A 0x20              /* 1=accessed, 0=not acccessed. */
#define PTE_D 0x40              /* 1=dirty, 0=not dirty (PTEs only). */

/**
 * Page Directory Entry
 * 
 */
static inline uint32_t pde_create (uint32_t *pt) { // 3.4GB
  ASSERT (pg_ofs (pt) == 0); // 0-12 bits, since 4KB when palloc()
  return vtop (pt) | PTE_U | PTE_P | PTE_W; // 0.4GB
}

static inline uint32_t *pde_get_pt (uint32_t pde) { // 0.4GB
  ASSERT (pde & PTE_P);
  return ptov (pde & PTE_ADDR); // 3.4GB & 32-12 bits 
}

/**
 * Page Table Entry
 * 
 */

// create() kernel pte -> VA to PA
// returns a PTE that points to page(kpage/frame page) of actual data
// flags 3 bits: user, writable, present
// only call once when init() kernel page_dir
// e.g. page = 3.7GB (kpage)
static inline uint32_t pte_create_kernel (void *page, bool writable) {
  ASSERT (pg_ofs (page) == 0);
  return vtop (page) | PTE_P | (writable ? PTE_W : 0); // 0.7 GB
}

// call when create() user page_dir
static inline uint32_t pte_create_user (void *page, bool writable) {
  return pte_create_kernel (page, writable) | PTE_U;
}

// return VA, with only the first 20 bits
static inline void *pte_get_page (uint32_t pte) { // 0.7GB
  return ptov (pte & PTE_ADDR); // 3.7
}

#endif /* threads/pte.h */

