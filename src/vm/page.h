#ifndef VM_PAGE_H
#define VM_PAGE_H

#include "vm/frame.h"
#include "vm/swap.h"
#include <hash.h>
#include "filesys/off_t.h"

enum kpage_status {
  ALL_ZERO,         // All zeros
  ON_SWAP,          // disk, raw disk page
  ON_FILESYS,       // disk, executable indexed by fs
  ON_FRAME          // for vm_unload_kpage(), not vm_load_kpage()
};

/**
 * data structure of each thread's VA's hardware status n position
 * -> 1 supt per thread
 * -> u() supt and page() + swap_out(), file_write()
 * -> abstract frametable(which abstracts pagedir and swap)
 * -> provides APIs for mmap(), munmap(), sys_read(), sys_write(), load_elf(), setup_user_stack(), page_fault()
 * -> APIs: vm_install_supt(), vm_load_supt(), vm_unload_supt()
 * 
 */ 
struct supt_entry{
  void *upage; // faulty VA == unique key (of each thread)
  
  enum kpage_status status; // VA's physical status
  int32_t content_bytes, zero_bytes; // kpage_size = occupied_bytes - zero_bytes, int32_t to store error -1

  // ON FRAME
  void *kpage;//munmap(): free() kpage, delete() frametable, pagedir

  // FILE SYSTEM 
  // store args of load_elf() n mmap(), for file_read() when page_fault() later
  struct file *file; // file_read() needs inode
  off_t file_offset; // for file_seek(), offset of entire file upage mapped to if mmap(), determined by elf phdr if load_elf()
  bool writable; // true if mmap(), determined by elf phdr if load_elf()
  
  // ON SWAP 
  uint32_t swap_index; // file_read(), free() swap bitmap
  bool dirty; // kpage evicted but not flush() to original file yet

  struct hash_elem supt_elem; // supple_page_table == hashtable
};

struct hash *vm_supt_init(void);

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