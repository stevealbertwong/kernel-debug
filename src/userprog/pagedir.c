#include "userprog/pagedir.h"
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include "threads/init.h"
#include "threads/pte.h"
#include "threads/palloc.h"

static uint32_t *active_pd (void);
static void invalidate_pagedir (uint32_t *);

/***************************************************************************************
 * key helpers
 * 
 * 
 * 
 * 
 * 
*************************************************************************************/
// traverse VA pages, return VA
// e.g pd: 3.2GB, VA: 0.01GB 0.02GB 0.03GB (32 bits user ps addr)
static uint32_t *
lookup_page (uint32_t *pd, const void *vaddr, bool create) 
{
  uint32_t *pt, *pde;
  ASSERT (pd != NULL);  
  ASSERT (!create || is_user_vaddr (vaddr)); // kernel page_dir is unchanged

  pde = pd + pd_no (vaddr); // 3.21GB = 3.2GB + 0.01 
  if (*pde == 0) // deref 3.21GB, not exist yet, creates new mapping
    {
      if (create)
        {
          pt = palloc_get_page (PAL_ZERO); // 3.4GB
          if (pt == NULL) 
            return NULL;       
          *pde = pde_create (pt); // *(3.21GB) = 0.4GB
        }
      else
        return NULL;
    }    
  pt = pde_get_pt (*pde); // 3.4GB = 0.4GB
  return &pt[pt_no (vaddr)]; // return 3.4GB + 0.02GB
}


/***************************************************************************************
 * key APIs
 * 
 * 
 * 
 * 
 * 
*************************************************************************************/
// create user page_dir, called by process.c load() only once 
uint32_t *
pagedir_create (void) 
{  
  uint32_t *pd = palloc_get_page (0); // 3.2GB (user page_dir in kernel pool)
  if (pd != NULL)
    // seed every user page_dir with only global kernel page_dir at first
    memcpy (pd, init_page_dir, PGSIZE);
  return pd; // VA
}

// add mapping in user pagedir
// upage is faulty addr from ELF, kpage from palloc() user pool
// e.g. pd = 3.2GB, kpage = 3.7GB, upage = 0.01GB 0.02GB 0.03GB (32 bits user ps addr)
bool
pagedir_set_page (uint32_t *pd, void *upage, void *kpage, bool writable)
{
  uint32_t *pte;
  ASSERT (pg_ofs (upage) == 0); // upage aligned with 4KB
  ASSERT (pg_ofs (kpage) == 0);
  ASSERT (is_user_vaddr (upage));
  ASSERT (vtop (kpage) >> PTSHIFT < init_ram_pages);
  ASSERT (pd != init_page_dir);

  pte = lookup_page (pd, upage, true); // 3.42GB = 3.2GB, 0.01 0.02 0.03GB
  if (pte != NULL) // UPAGE must not already be mapped
    {
      ASSERT ((*pte & PTE_P) == 0);
      *pte = pte_create_user (kpage, writable); // *(3.42GB) = 0.7GB
      return true;
    }
  else // upage already mapped before
    return false;
}

// return kpage mapped in user pagedir 
// e.g. pd = 3.2GB, upage = 0.01GB 0.02GB 0.03GB (32 bits user ps addr)
void *
pagedir_get_page (uint32_t *pd, const void *uaddr) 
{
  uint32_t *pte;
  ASSERT (is_user_vaddr (uaddr));
  pte = lookup_page (pd, uaddr, false); // 3.42GB = 3.2GB, 0.01 0.02 0.03GB
  if (pte != NULL && (*pte & PTE_P) != 0)    
    return pte_get_page (*pte) + pg_ofs (uaddr); // 3.7GB + offset bits
  else
    return NULL;
}

/**
 * called once when process_exit()
 * 
 * palloc_free() cmdline, elf code(eip), stack(esp)
 * 
 */ 
void
pagedir_destroy (uint32_t *pd)
{
  uint32_t *pde;
  if (pd == NULL)
    return;

  ASSERT (pd != init_page_dir);
  // free() all pages pagedir reference 
  for (pde = pd; pde < pd + pd_no (PHYS_BASE); pde++)
    if (*pde & PTE_P) 
      {
        uint32_t *pt = pde_get_pt (*pde);
        uint32_t *pte;
        
        for (pte = pt; pte < pt + PGSIZE / sizeof *pte; pte++)
          if (*pte & PTE_P) 
            palloc_free_page (pte_get_page (*pte));
        palloc_free_page (pt);
      }
  palloc_free_page (pd);
}

/* Marks user virtual page UPAGE "not present" in page
   directory PD.  Later accesses to the page will fault.  Other
   bits in the page table entry are preserved.
   UPAGE need not be mapped. */
void
pagedir_clear_page (uint32_t *pd, void *upage) 
{
  uint32_t *pte;

  ASSERT (pg_ofs (upage) == 0);
  ASSERT (is_user_vaddr (upage));

  pte = lookup_page (pd, upage, false);
  if (pte != NULL && (*pte & PTE_P) != 0)
    {
      *pte &= ~PTE_P;
      invalidate_pagedir (pd);
    }
}


/***************************************************************************************
 * helper
 * 
 * 
 * 
 * 
 * 
*************************************************************************************/

void
pagedir_activate (uint32_t *pd) 
{
  if (pd == NULL)
    pd = init_page_dir;

  /* Store the physical address of the page directory into CR3
     aka PDBR (page directory base register).  This activates our
     new page tables immediately.  See [IA32-v2a] "MOV--Move
     to/from Control Registers" and [IA32-v3a] 3.7.5 "Base
     Address of the Page Directory". */
  asm volatile ("movl %0, %%cr3" : : "r" (vtop (pd)) : "memory");
}

/* Returns the currently active page directory. */
static uint32_t *
active_pd (void) 
{
  /* Copy CR3, the page directory base register (PDBR), into
     `pd'.
     See [IA32-v2a] "MOV--Move to/from Control Registers" and
     [IA32-v3a] 3.7.5 "Base Address of the Page Directory". */
  uintptr_t pd;
  asm volatile ("movl %%cr3, %0" : "=r" (pd));
  return ptov (pd);
}

bool
pagedir_is_dirty (uint32_t *pd, const void *vpage) 
{
  uint32_t *pte = lookup_page (pd, vpage, false);
  return pte != NULL && (*pte & PTE_D) != 0;
}

/* Set the dirty bit to DIRTY in the PTE for virtual page VPAGE
   in PD. */
void
pagedir_set_dirty (uint32_t *pd, const void *vpage, bool dirty) 
{
  uint32_t *pte = lookup_page (pd, vpage, false);
  if (pte != NULL) 
    {
      if (dirty)
        *pte |= PTE_D;
      else 
        {
          *pte &= ~(uint32_t) PTE_D;
          invalidate_pagedir (pd);
        }
    }
}

bool
pagedir_is_accessed (uint32_t *pd, const void *vpage) 
{
  uint32_t *pte = lookup_page (pd, vpage, false);
  return pte != NULL && (*pte & PTE_A) != 0;
}

void
pagedir_set_accessed (uint32_t *pd, const void *vpage, bool accessed) 
{
  uint32_t *pte = lookup_page (pd, vpage, false);
  if (pte != NULL) 
    {
      if (accessed)
        *pte |= PTE_A;
      else 
        {
          *pte &= ~(uint32_t) PTE_A; 
          invalidate_pagedir (pd);
        }
    }
}

static void
invalidate_pagedir (uint32_t *pd) 
{
  if (active_pd () == pd) 
    {
      /* Re-activating PD clears the TLB.  See [IA32-v3a] 3.12
         "Translation Lookaside Buffers (TLBs)". */
      pagedir_activate (pd);
    } 
}
