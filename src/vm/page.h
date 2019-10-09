#ifndef VM_PAGE_H
#define VM_PAGE_H

#include "vm/frame.h"
#include "vm/swap.h"
#include <hash.h>
#include "filesys/off_t.h"



// APIs: load, unload, evict 
void* vm_load_kpage_using_supt(struct hash *supt, uint32_t *pagedir, void *upage);
bool vm_supt_unload_kpage(struct hash *supt, uint32_t *pagedir,
    void *upage, struct file *f, off_t offset, size_t bytes);
bool vm_supt_evict_kpage(struct frame_table_entry *evict_candidate);

// install 
bool vm_supt_install_frame(struct hash *supt, void *upage, void *kpage);
bool vm_supt_install_zero_page(struct hash *supt, void *upage);
bool vm_supt_install_swap(struct hash *supt, void *upage, uint32_t swap_index);
bool vm_supt_install_filesystem(struct hash *supt, void *upage, struct file *file, 
    off_t offset, uint32_t content_bytes, uint32_t zero_bytes, bool writable);

// getter setter
struct supt_entry *vm_supt_search_supt(struct hash *supt, void *upage);
bool vm_pin_upage(struct hash *supt, void *upage);
bool vm_unpin_upage(struct hash *supt, void *upage);
void vm_spte_set_dirty(struct supt_entry *spte);

#endif