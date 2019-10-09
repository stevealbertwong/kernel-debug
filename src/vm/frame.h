#ifndef VM_FRAME_H
#define VM_FRAME_H

#include <hash.h>
#include "lib/kernel/hash.h"

#include "threads/synch.h"
#include "threads/palloc.h"



/**
 * data structure to EVICT kpage to swap
 * -> 1 global frame table (not each thread) in kernel stack
 * -> kpage == unique key
 * -> API: vm_palloc_kpage(), vm_free_kpage()
 * -> u() frametable + u() pagedir dirty, access bit + palloc()
 * 
 */ 
struct frame_table_entry{
    void *kpage; // unique key (frame page == from upool 3.5-4GB == VA's real physcial addr)
    
    // to get() n u() other vm data structure when evict() 
    // retrieve t->pagedir{}, supt{}, accessed, dirty bit
    struct thread *owner_thread; // evict() could be called another thread ?? shared pages ??
    void *upage; // unique key of pagedir{}, supt{} when u()

    // TODO: int pin_count; share pages ??
    bool pinned; // no evict, e.g. mmap(), I/O r() w() blocks of disk file

    struct list_elem frame_table_elem; // list element of frame table
    struct hash_elem frame_table_hash_elem; // list element of frame table
};


void vm_frametable_init(void);
void *vm_palloc_kpage(enum palloc_flags flags, void *upage);
void vm_free_kpage(void *kpage);

void vm_pin_kpage(void *kpage);
void vm_unpin_kpage(void *kpage);

#endif /* vm/frame.h */