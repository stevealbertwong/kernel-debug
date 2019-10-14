/**
 * ??
 * pinned in sup_page_entry -  This is set to true whenever the kernel is
 * accessing the data in the spte. Thus, this means whenever kernel threads
 * are accessing a frame, it cannot be evicted. This is key in preventing
 * kernel crashes.
 * -> shouldn't pinned be in frame ??
 */
#include <hash.h>
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "lib/kernel/hash.h"

#include "threads/synch.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "threads/palloc.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "vm/page.h"
#include "vm/frame.h"
#include "vm/swap.h"
#include "filesys/file.h"

static unsigned supt_hash_func(const struct hash_elem *elem, void *aux);
static bool     supt_less_func(const struct hash_elem *, const struct hash_elem *, void *aux);
void* vm_load_kpage_from_filesystem(struct supt_entry *spte, void *kpage);
void* vm_load_kpage_from_swap(struct supt_entry *spte, void *kpage);
void* vm_load_kpage_all_zeros(void *kpage);



/**
 * 1 thread == 1 supt, no global supt
 * supt in kernel pool 3-3.5GB PA
 */ 
struct hash *vm_supt_init(void){
  struct hash *supt = (struct hash*) malloc(sizeof(struct hash));
  // printf("vm_supt_init a  \n");
  hash_init(supt, supt_hash_func, supt_less_func, NULL);
  // printf("vm_supt_init b \n");
  return supt;
}


/*******************************************************************/
// major APIs
// 1. load kpage for upage
// 2. unload kpage for upage      e.g. munmap()
// 3. append spte(upage's physical status) to supt
//    - e.g. evict(), mmap(), load_elf(), alloc_user_stack(), stack_grow()






/*******************************************************************/
/**
 * file_read() kpage from its supt hardware status to RAM
 * 
 * called by page_fault_handler(intr_frame) 
 * + syscall.c sys_read(fd, buffer, size)
 * 
 * 1. search supt for where kpage is (RAM/Disk)
 * 2. vm_palloc_kpage() new empty kpage to store 
 * 3. u() pagedir{}, spte{}
 * 
 * RMB to unpin() kpage !!!!!!!!!!
 */ 
void*
vm_load_kpage_using_supt(struct hash *supt, uint32_t *pagedir, void *upage)
{
  // 1. search supt for where kpage is (RAM/Disk)
  struct supt_entry *spte = vm_supt_search_supt(supt, upage);
  if(spte->status == ON_FRAME) { // pagedir wont allow set pagedir twice 
    return NULL;
  }
  if(spte == NULL){
    PANIC("vm_load_kpage_using_supt() upage does not have spte entry \n");
  }

  // 2. vm_palloc_kpage() new empty kpage to store 
  void* kpage = vm_palloc_kpage(PAL_USER, upage); // pinned 
  if(kpage == NULL){
    PANIC("vm_load_kpage_using_supt() vm_palloc_kpage() failed \n"); 
  }
  printf("kpage loaded: %d \n", spte->status);
  switch (spte->status)
  {
  case ON_FILESYS:
    vm_load_kpage_from_filesystem(spte, kpage); // file_read()
    break;
  case ON_SWAP:
    vm_load_kpage_from_swap(spte, kpage); // swap_in()
    break;
  case ALL_ZERO:
    vm_load_kpage_all_zeros(kpage); // memset(0)
    break;
  case ON_FRAME:
    // upage is PAGESIZE of many faulty addr, pagedir not be set twice
    // e.g. load_elf() then read() access same upage    
    PANIC("vm_load_kpage_using_supt() upage's is already on frame, why load again to frame, page_fault() file_read() both should not be on frame \n");
    break;

  default:
    PANIC("vm_load_kpage_using_supt() supt->status \n");
  }

  vm_unpin_kpage(kpage); // unpin once done file_read()
  
  // 3. u() supt + pagedir
  lock_acquire(&thread_current()->supt_lock);
  spte->status = ON_FRAME;
  spte->kpage = kpage;
  
  // printf("vm_load_page() upage:%x, kpage:%x \n", upage, kpage);
  if(!pagedir_set_page(pagedir, upage, kpage, spte->writable)){
    PANIC("vm_load_kpage_using_supt() pagedir_set_page() failed \n");
  }
  // fresh kpage is clean in pagedir
  pagedir_set_dirty(pagedir, kpage, false);
  lock_release(&thread_current()->supt_lock);

  return kpage;
};




/**
 * munmap() == flush() dirty kpages to orignal file 
 * == how mmap() save changes to file without user explicit code
 * == only mmap() file has dirty pages to flush 
 * 
 * called by system_call_munmap(mmapid_t mid)
 * 
 * 1. check supt for where upage is (RAM/Disk)
 * 
 * 2 delete() vm data structure + flush dirty kpages if mmap() file is writable
 * - segfault instead of pagefault
 * 
 * 2.1 mmap() file not been page fault n loaded in RAM yet (FILESYS):
 * - no need to flush() dirty page or delete() vm data structure
 * - delete() upage's spte from supt 
 * 
 * 2.2 mmap() file already page fault, kpage in RAM (FRAME):
 * - flush() dirty page
 * - delete() kpage + frame_table
 * - delete() pagedir 
 * - delete() upage's spte from supt
 * 
 * 2.3 if already page fault, page evicted to swap disk (SWAP):
 * - flush() dirty page from swap to kpage to fs
 * - delete() swap bitmap 
 * - delete() upage's spte from supt
 * 
 */ 
bool vm_supt_unload_kpage(struct hash *supt, uint32_t *pagedir,
    void *upage, struct file *f, off_t offset, size_t bytes)
{// args are stored in mmap_descriptor{}
    struct supt_entry *spte = vm_supt_search_supt(supt, upage);
    if(!spte || !spte->kpage){
      PANIC("vm_supt_unload_kpage() no spte \n");
    }
    switch (spte->status){

      // 2.1 mmap() file not been page fault n loaded in RAM yet
      case ON_FILESYS: 
        // no need to flush() dirty page or delete() vm data structure
        break;

      // 2.2 mmap() file already page fault, kpage in RAM (FRAME):
      case ON_FRAME:
        // original mmap() file writable        
        if(spte->writable && (pagedir_is_dirty(pagedir, upage) || spte->dirty)){
            file_seek(f, offset);
            file_write(f, spte->kpage, bytes); // upage/kpage ??
            // file_write_at (f, spte->upage, bytes, offset);
        }
        vm_free_kpage(spte->kpage); // frametable + palloc = frame.c
        pagedir_clear_page(pagedir, upage);
        break;


      // 2.3 if already page fault, page evicted to swap disk (SWAP):
      // if swap, no pagedir, frametable
      case ON_SWAP:
        if(spte->writable && (pagedir_is_dirty(pagedir, upage) || spte->dirty)){ 
          
          void *kpage = vm_palloc_kpage(PAL_USER, upage);
          vm_swap_read_kpage_from_disk (spte->swap_index, kpage); // includes swap_free()
          file_write_at (f, kpage, PGSIZE, offset);
          vm_free_kpage(kpage);
          
          // alternatively(kernel pool): 
          // void *tmp_page = palloc_get_page(0); // kernel pool
          // palloc_free_page(tmp_page);
          
        }else{
          vm_swap_free (spte->swap_index);
        }
        break;
      
      case ALL_ZERO:
        PANIC("vm_unload_upage_delete_supt() mmap() upage not supposed to be all zeros \n");
        break;


    }
    hash_delete(supt, &spte->supt_elem); // delete() spte
    
    return true;
}




/**
 * 
 *  - pagedir
 *  - supt
 *  - frametable
 *  - swap
 *  - palloc
 * 
 * NOTE: order does not matter ??
 */ 
bool
vm_supt_evict_kpage(struct frame_table_entry *evict_candidate){
  struct thread *owner = evict_candidate->owner_thread;
  if(!owner){
    PANIC("vm_supt_evict_kpage() no thread \n");
  }
  
  // dirty pages, record CPU dirty bit(pagedir) in supt, only certain are meant to make changes to original file
  // flush dirty pages only when unmmap() !!!! 
  // BUG: pagedir_is_dirty(kpage) ??
  if(pagedir_is_dirty(owner->pagedir, evict_candidate->upage)){

    // vm_supt_set_dirty(f_evicted->t->supt, f_evicted->upage, is_dirty); // u() supt ??
    struct supt_entry *spte = vm_supt_search_supt(owner->supt, evict_candidate->upage);
    if(!spte){
      PANIC("vm_supt_evict_kpage() vm_supt_search_supt() failed \n");
    }
    spte->dirty = true;

  }
  pagedir_clear_page(owner->pagedir, evict_candidate->upage);  

  uint32_t swap_idx = vm_swap_flush_kpage_to_disk(evict_candidate->kpage);
  if(!swap_idx){
    PANIC("vm_supt_evict_kpage() vm_swap_flush_kpage_to_disk() failed \n");
  }  
  vm_supt_install_swap(owner->supt, evict_candidate->upage, swap_idx);      
  vm_free_kpage(evict_candidate->kpage);

  return true; 
}

/*******************************************************************/
// creates entry in supt
// u() supt when load elf, page fault, mmap







/*******************************************************************/

/**
 * u() supt kpage is on filesystem(disk)
 * 
 * called when lazy loading -> e.g. mmap(), load_elf() elf code in disk
 * args are from mmap() layer, load_elf() elf header
 * 
 */ 
bool vm_supt_install_filesystem(struct hash *supt, void *upage, struct file *file, 
    off_t offset, uint32_t content_bytes, uint32_t zero_bytes, bool writable)
{  
  // vm_supt_search_supt(supt, upage); ??
  struct supt_entry *spte = (struct supt_entry *) malloc(sizeof(struct supt_entry));
  if(!spte){
    PANIC("vm_supt_on_filesystem() malloc failed \n");
  }
  spte->status = ON_FILESYS;
  spte->upage = upage;
  spte->file = file;
  spte->dirty = false;
  spte->file_offset = offset;
  spte->content_bytes = content_bytes;
  spte->zero_bytes = zero_bytes;
  spte->writable = writable;

  if(!hash_insert (supt, &spte->supt_elem)){
    return true; // successfully inserted spte into supt
  }else{
    PANIC("vm_supt_on_filesystem() failed to insert into supt \n");
    return false;
  }
}


/**
 * u() supt kpage is on swap(disk)
 * 
 * called by evict_to_swap()
 * / frame_allocate() after swap kpage out in eviction
 * 
 */ 
bool vm_supt_install_swap(struct hash *supt, void *upage, uint32_t swap_index){
  // upage spte should have existed !!!!
  struct supt_entry *spte = vm_supt_search_supt(supt, upage);
  if(spte == NULL) {
    PANIC("vm_supt_on_swap() upage does not exist in supt \n");
    return false;
  }

  spte->status = ON_SWAP;
  spte->swap_index = swap_index;
  spte->dirty = false;
  spte->writable = true;
  return true;
}



/**
 * u() supt kpage is fresh stack zero page
 * 
 * called by allocate_user_stack()
 * 
 */ 
bool vm_supt_install_zero_page(struct hash *supt, void *upage){
  // vm_supt_search_supt(supt, upage); ??

  struct supt_entry *spte = (struct supt_entry *) malloc(sizeof(struct supt_entry));
  if(!spte){
    PANIC("vm_supt_zero_page() malloc failed \n");
  }
  spte->status = ALL_ZERO;
  spte->upage = upage;
  spte->dirty = false;
  spte->writable = true;

  if(!hash_insert (supt, &spte->supt_elem)){
    return true; // successfully inserted spte into supt
  }else{
    PANIC("vm_supt_zero_page() failed to insert into supt \n");
    return false;
  }

}

/**
 * u() supt kpage is on frame, for munmap()
 * 
 * called by install_pages(), which is called by load_elf(), setup_user_stack()
 */ 
bool vm_supt_install_frame(struct hash *supt, void *upage, void *kpage){

  struct supt_entry *spte = (struct supt_entry *) malloc(sizeof(struct supt_entry));
  if(!spte){
    PANIC("vm_supt_zero_page() malloc failed \n");
  }
  spte->status = ON_FRAME;
  spte->upage = upage;
  spte->kpage = kpage;
  spte->dirty = false;
  spte->writable = true;

  if(!hash_insert (supt, &spte->supt_elem)){
    return true; // successfully inserted spte into supt
  }else{
    PANIC("vm_supt_zero_page() failed to insert into supt \n");
    return false;
  }
}



/*******************************************************************/
// vm_load_kpage() from disk helpers






/*******************************************************************/
/**
 * mmap() == lazy load using supt, page fault to trigger
 */ 
void* vm_load_kpage_from_filesystem(struct supt_entry *spte, void *kpage){
  memset (kpage, 0, PGSIZE); // empty part of page should be zeros  
  file_seek(spte->file, spte->file_offset); //file offset upage mapped to  
  if(file_read(spte->file, kpage, spte->content_bytes)!= spte->content_bytes){
    PANIC("vm_load_kpage_from_filesystem() failed to file_read()");
  }

  return kpage;
}


void* vm_load_kpage_from_swap(struct supt_entry *spte, void *kpage){
  vm_swap_read_kpage_from_disk(spte->swap_index, kpage);  
  return kpage;
}


void* vm_load_kpage_all_zeros(void *kpage){
  memset(kpage, 0, PGSIZE);
  return kpage;
}


/*******************************************************************/
// other helpers













/*******************************************************************/

struct supt_entry *
vm_supt_search_supt(struct hash *supt, void *upage){

  struct supt_entry spte; // temp stack object, no need malloc() then free()
  spte.upage = upage;  

  struct hash_elem *elem = hash_find (supt, &spte.supt_elem);
  if(elem == NULL){
    // PANIC("vm_supt_search_supt() failed to find spte\n");
    return NULL;
  } else{
    return hash_entry(elem, struct supt_entry, supt_elem);
  }
}

void
vm_spte_set_dirty(struct supt_entry *spte){
  spte->dirty = true;
}

bool 
vm_pin_upage(struct hash *supt, void *upage){
  struct supt_entry *spte = vm_supt_search_supt(supt, upage);
  if((!spte->kpage) || (!spte)){
    PANIC("vm_pin_upage() failed, spte no kpage \n");
  }
  vm_pin_kpage(spte->kpage);
  return true;
}

bool 
vm_unpin_upage(struct hash *supt, void *upage){
  struct supt_entry *spte = vm_supt_search_supt(supt, upage);
  if((!spte->kpage) || (!spte)){
    PANIC("vm_pin_upage() failed, spte no kpage \n");
  }
  vm_unpin_kpage(spte->kpage);
  return true;
}


static unsigned
supt_hash_func(const struct hash_elem *elem, void *aux UNUSED)
{
  struct supt_entry *entry = hash_entry(elem, struct supt_entry, supt_elem);
  return hash_int( (int)entry->upage );
}


static bool
supt_less_func(const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED)
{
  struct supt_entry *a_entry = hash_entry(a, struct supt_entry, supt_elem);
  struct supt_entry *b_entry = hash_entry(b, struct supt_entry, supt_elem);
  return a_entry->upage < b_entry->upage;
}