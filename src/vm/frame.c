#include <stdio.h>
#include <stdbool.h>
#include <stddef.h>
#include <hash.h>
#include <list.h>

#include "threads/thread.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "userprog/pagedir.h"
#include "threads/vaddr.h"
#include "vm/page.h"
#include "vm/frame.h"

#include "lib/kernel/hash.h"
#include "lib/kernel/list.h"

struct frame_table_entry* vm_clock_ptr_circular_loop(void);

static struct list frame_table;
static struct hash frame_table_hash; // for quick look up
static struct lock frame_table_lock;
static struct list_elem *clock_hand; // for circular loop when evict
static unsigned frametable_hash_func(const struct hash_elem *elem, void *aux UNUSED);
static bool frametable_less_func(const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED);
struct frame_table_entry* vm_pick_kpage_to_evict(void);
void vm_append_frame_table(void *kpage, void *upage);
struct frame_table_entry* vm_search_frametable(void* kpage);

/**
 * 1 global frame table (not each thread)
 * 
 * called by init.c main()
 */ 
void vm_frametable_init(){  
  list_init (&frame_table);
  hash_init (&frame_table_hash, frametable_hash_func, frametable_less_func, NULL);
  lock_init (&frame_table_lock);  
}


/*******************************************************************/
// major APIs
// 1. "infinite" palloc_kpage() by evicting "old" kpage 
// 2. free_kpage()







/*******************************************************************/
/**
 * get kpage from userpool. if full, evict.
 * called by load_elf(), allocate_user_stack(), vm_load_kpage_from_supt()
 * 
 * 1. palloc(userpool) + u() frametable[]
 *  - new empty kpage
 * 
 * 2.1 evict() if palloc(userpool) is full
 *  - pick kpage to evict
 *  - flush() selected to swap 
 * 
 * 2.2 u() other VM data structure for eviction and new kpage
 *  - vm_supt_on_swap()
 *  - delete pagedir{}
 *  - swap_out_to_disk()
 *  - palloc_free(), then palloc(userpool) 
 */ 
void*
vm_palloc_kpage(enum palloc_flags flags, void *upage)
{
  void* kpage = palloc_get_page (PAL_USER | flags);
  if(!kpage){
    // 2.1 pick frame_table_entry{} to evict if palloc(userpool) is full
    struct frame_table_entry *evict_candidate = vm_pick_kpage_to_evict();
    if(evict_candidate == NULL || evict_candidate->owner_thread == NULL){
      PANIC("vm_palloc_kpage() vm_pick_kpage_to_evict() failed \n");
    }

    // 2.2 evict() + u() vm data structure for eviction
    bool success = vm_supt_evict_kpage(evict_candidate);    
    if(!success){
      PANIC("vm_supt_evict_kpage() failed \n");
    }
    kpage = palloc_get_page (PAL_USER | flags);
    if(!kpage){
      PANIC("vm_palloc_kpage() still no free kpage after eviction\n");
    }    
  }
  
  // 2.2 u() frametable and pagedir for fresh kpage    
  vm_append_frame_table(kpage, upage);    
  return kpage;    
}


/**
 * clock algo
 * pick 1st frame_table_entry{} to delete() from frame_table[]
 * 
 * 1. clock hand circular loop a circular list
 * 
 * 2. dont evict if (hierarchy to approximate LRU)
 * - pinned(frametable): never
 * - assessed(pagedir): second chance
 * - dirty(pagedir): if no choice, nothing w receny, just takes time to I/O disk
 *    - option 1: store "dirty bit" in supt, file_write() dirty page only when munmap()
 *    - option 2: file_write() dirty page each time evict, no need store "dirty bit" in supt
 * - limit infinite loop to 2nd chance
 * 
 * TODO: shared pages + supt
 * NOTE: flush dirty page when evict() or munmap() ?? depends if you save dirtiness in supt ??
 *
 */ 
struct frame_table_entry* 
vm_pick_kpage_to_evict(void){
  lock_acquire (&frame_table_lock);

  size_t i;
  for(i = 0; i <= hash_size(&frame_table_hash) * 2; ++i) { // 2nd chance
    struct frame_table_entry *evict_candidate = vm_clock_ptr_circular_loop(); // infinite
    if(evict_candidate == NULL){
      PANIC("vm_pick_kpage_to_evict() evict_candidate is null \n");
    }

    if(evict_candidate->pinned){ // never evict
      continue;
    }
    
    struct thread *owner = evict_candidate->owner_thread;
    // 2nd chance: set accessed bit to 0, evict next time
    // BUG: pagedir_is_accessed(thread_current()->pagedir) ??
    if(pagedir_is_accessed(owner->pagedir, evict_candidate->upage)){
      pagedir_set_accessed(owner->pagedir, evict_candidate->upage, false);
      continue;
    }
    lock_release (&frame_table_lock);
    return evict_candidate;      
  }

  PANIC("vm_pick_kpage_to_evict() all kpages are pinned \n");
}



/**
 * free() kpage + frametable
 * must called after vm_palloc_kpage()
 * 
 * 1. delete entry from frametable
 * 2. palloc_free(kpage)
 * 
 * NOTE: u() supt all done in supt layer
 */ 
void
vm_free_kpage(void *kpage){
  struct frame_table_entry *e = vm_search_frametable(kpage);
  if(!e){
    PANIC("vm_free_kpage() no frame_table_entry of kpage \n");
  }

  hash_delete (&frame_table_hash, &e->frame_table_hash_elem);
  list_remove (&e->frame_table_elem);
  free(e);  
  if(!kpage){
    PANIC("vm_free_kpage() kpage is null \n");
  }
  palloc_free_page(kpage);

}

/*******************************************************************/
// helpers








/*******************************************************************/
/**
 * everythime when there is a fresh/updated kpage
 * called by vm_palloc_kpage()
 * 
 * 1. malloc() frametable entry
 * 2. populate()
 * 3. append frmae_table[]
 * 
 * NOTE: every fresh kpage is pinned !!!!!!
 */ 
void 
vm_append_frame_table(void *kpage, void *upage){
  
  lock_acquire (&frame_table_lock);

  struct frame_table_entry *frame = malloc(sizeof(struct frame_table_entry));
  if(frame == NULL) {
    PANIC("vm_append_frame_table() failed, kernel malloc() is full \n");
  }
  
  frame->kpage = kpage;
  frame->upage = upage;
  frame->owner_thread = thread_current ();
  frame->pinned = true; // every fresh kpage is pinned !!!!!!

  hash_insert (&frame_table_hash, &frame->frame_table_hash_elem);
  // both list_push_front() n back() are wrong !!!!!
  // should insert just before clock pointer, but we are just approximating
  list_push_back (&frame_table, &frame->frame_table_elem);
  lock_release (&frame_table_lock);

}



// move clock hand in circular list 
struct frame_table_entry* vm_clock_ptr_circular_loop(void)
{
  if (list_empty(&frame_table)){
    PANIC("clock_ptr_circular_loop() frametable empty \n");
  }
    
  // if end of list, restart 
  if (clock_hand == NULL || clock_hand == list_end(&frame_table))
    clock_hand = list_begin (&frame_table);
  else
    clock_hand = list_next (clock_hand);

  struct frame_table_entry *e = list_entry(clock_hand, struct frame_table_entry, frame_table_elem);
  return e;
}

void vm_pin_kpage(void *kpage){
  struct frame_table_entry *frame = vm_search_frametable(kpage);
  if(!frame){
    PANIC("vm_pin_kpage() vm_search_frametable() failed ");
  }
  frame->pinned = true;
}

void vm_unpin_kpage(void *kpage){
  struct frame_table_entry *frame = vm_search_frametable(kpage);
  if(!frame){
    PANIC("vm_unpin_kpage() vm_search_frametable() failed ");
  }
  frame->pinned = false;
}


struct frame_table_entry* 
vm_search_frametable(void* kpage){
  struct frame_table_entry e_temp; 
  e_temp.kpage = kpage;
  struct hash_elem *h = hash_find (&frame_table_hash, &(e_temp.frame_table_hash_elem));

  return hash_entry(h, struct frame_table_entry, frame_table_hash_elem);
}



static unsigned frametable_hash_func(const struct hash_elem *elem, void *aux UNUSED)
{
  struct frame_table_entry *entry = hash_entry(elem, struct frame_table_entry, frame_table_hash_elem);
  return hash_bytes( &entry->kpage, sizeof entry->kpage );
}


static bool frametable_less_func(const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED)
{
  struct frame_table_entry *a_entry = hash_entry(a, struct frame_table_entry, frame_table_hash_elem);
  struct frame_table_entry *b_entry = hash_entry(b, struct frame_table_entry, frame_table_hash_elem);
  return a_entry->kpage < b_entry->kpage;
}
