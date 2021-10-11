/**
 * BASIC: 
 * 1. clock algo 
 * 
 * 2. no external synch == finer grained synch == more than 1 thread can be running fs code at once
 * 
 * 3. read-through cache 
 * -> r() from cache
 * -> improves read performance
 * 
 * 4. write-through cache
 * -> immediately flush() to disk + u() cache 
 * -> dirty bit to stale cache page(inconsistent w disk page), first to evict 
 * -> does NOT improve write performance
 * 
 * 5. read-ahead cache 
 * -> fetch next page of a file into cache
 * -> next page should be asynch i.e. return control to user once original cache page read()
 * asynch == no lock, non blcok, in background, spawn new thread
 * 
 * 6. write-behind cache
 * -> X flush() immediately, store dirty block in cache
 * -> improve write performance BUT fragile to crashes
 * -> flush() dirty cache pages when: 
 * ---> evict
 * ---> spawn a thread timer_sleep() to periodically
 * ---> filesys_done(), so that halting Pintos flushes cache
 * 
 * 
 * ENHANCEMENT: 
 * 1. read write lock on cache pages
 * 2. TODO: always maintain a sorted LRU queue
 */ 
#include "filesys/inode.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/synch.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"

#define CACHE_NUM_DISK_SECTORS 64

struct cached_disk_page { // could exceeds 512kb since RAM
    block_sector_t disk_addr; // unique key
    uint8_t cached_page[512];// each cahce page == 512kb
    bool empty;

    /* eviction data structure */
    bool accessed; // clock algo
    bool dirty; // stale cache page, evict first 
    
    /*  R/W lock data structure */
    size_t num_readers; // num of readers reading atm
    bool writing; // writer writing, 1 by 1
    struct condition readers; // store blocked reader threads 
    struct condition writers; // store blocked writer threads 
    struct lock cache_entry_lock;

}

/**
 * called only by reader 
 * 
 */ 
read_lock_acquire(struct cached_disk_page cache, struct lock cache_entry_lock){
    // 1. acquire lock first !!!!!!
    lock_acquire(&cache_entry_lock);

    if(!cache.writing){ // 1. if no writers yet, proceeds
        ASSERT(list_empty(&cache.writers.waiters));

    } else { // 2. if already writers, reader block itself
        // reader blocked until all writers finishes and broadcast
        cond_wait(&cache.readers, &cache_entry_lock);
    }

    // 3. read lock acquired 
    cache.num_readers++;
    lock_release(&cache_entry_lock);
}

/**
 * called only by reader 
 * 
 */
read_lock_release(struct cached_disk_page cache, struct lock cache_entry_lock){
    ASSERT(list_empty(&cache.readers.waiters)); // there shouldn't be any blocked readers when reading mode

    // only last reader need to broadcast to writer
    if(cache.num_readers == 1){
        // cond_broadcast(&cache.readers, &cache_entry_lock); 
        cond_broadcast(&cache.writers, &cache_entry_lock);
    }

    cache.num_readers--;
    lock_release(&cache_entry_lock);    
}


/**
 * called only by writer 
 * 
 */
write_lock_acquire(struct cached_disk_page cache, struct lock cache_entry_lock){

    // 1. if already writer/readers, writer block itself first
    if(cache.writing || cache.num_readers > 0 || !list_empty(&cache.writers.waiters)){
        cond_wait(&cache.writers, &cache_entry_lock);
    }
    
    // 2. gets write lock, reader broadcast or writer signal
    cache.writing = true;
    lock_release(&cache_entry_lock);   
}

/**
 * 
 * writer has priority over reader 
 */
write_lock_release(struct cached_disk_page cache, struct lock cache_entry_lock){
    // 1. each writer signal next waiting writer to start
    if(list_size(&cache.writers.waiters) > 0){ 
        cond_signal(&cache.writers, &cache_entry_lock); // next writer in line

    // 2. last writer broadcast to all waiters        
    } else if (list_size(&cache.writers.waiters) == 0){
        // put all read and write waiters back to ready_queue
        cond_broadcast(&cache.readers, &cache_entry_lock); // cache_entry_lock UNUSED
        cond_broadcast(&cache.writers, &cache_entry_lock);
    } else {
        PANIC("write_lock_release() failed \n");
    }
    
    cache.writing = false;
    lock_release(&cache_entry_lock);  
}



struct cached_disk_page cache_table[CACHE_NUM_DISK_SECTORS]; // kernel stack


cache_block_write (fs_device, &inode_disk->direct_disk_addrs[i], zeros);

cache_block_read (fs_device, inode->sector, &inode->inode_disk);


bool
cache_init(){
    // init empty disk pages at kernel stack level ??

    thread_create("filesys_cache_writeback", 0, thread_func_write_back, NULL);
}

/***************************************************************/
// Major APIs


/***************************************************************/

void
cache_block_read(block_sector_t disk_addr, void *buffer, size_t num_bytes){
    
    // 1. look inside cache first, set accessed bit
    for (size_t i = 0; i < CACHE_SIZE; i++){
        read_lock_acquire(cache_table[i]);
        if(cache_table[i]->disk_addr == disk_addr){
            read_lock_release(cache_table[i]);

            write_lock_acquire(cache_table[i], cache_table[i]->lock);
            memcpy(cache_table[i], buffer);
            cache_table[i]->accessed = true;
            write_lock_release(cache_table[i], cache_table[i]->lock);
            
            return;
        }
        read_lock_release(cache_table[i]);
    }

    // 2. find empty cache, if no empty cache, evict 
    // update cache with new disk data
    // what if cache page is dirty ??
    struct cached_disk_page cache_entry = search_empty_cache();
    if(!cache_entry){
        if(!clock_algo_evict_cache(disk_addr, buffer)){
            PANIC("cache_block_read() evict cache slot failed \n");
        }
    }

    // 3. block_read() into empty cache first, then to buffer 
    write_lock_acquire(cache_table[i], cache_table[i]->lock);
    cache_table[i]->disk_addr = disk_addr;
    cache_table[i]->empty = false;            
    cache_table[i]->accessed = true;
    cache_table[i]->dirty = false;       
    block_read(disk_addr, buffer, num_bytes);    
    memcpy(buffer, cache_table[i]->cached_page);
    write_lock_release(cache_table[i], cache_table[i]->lock);

    // 4. read ahead child thread
    thread_create(read_ahead_thread());    
    // parent wait until child finishes
    struct thread *parent_thread = thread_current(); 
    sema_down(&parent_thread->sema_read_ahead);

}


/**
 * 
 * 
 */
cache_block_write(block_sector_t disk_addr, void *buffer, size_t num_bytes){
    // 1. if exists in cache, marks dirty so to be evicted first 

    // 2. u() cache page dirty bit + block_write()
    // BUT ISN'T DIRTY PAGE IMMEDIATELY FLUSHED ?? UNLESS GROUP FLUSH LATER ??
    // what is the point of keeping a dirty page in cache that is dirty to be read ??

    // 3. clock algo replace new cache
    clock_algo_evict_cache()

}


/***************************************************************/
// helpers






/***************************************************************/

struct cached_disk_page 
search_empty_cache(){
    for (size_t i = 0; i < CACHE_SIZE; i++){
        read_lock_acquire(cache_table[i]);        
        if(cache_table[i]->empty){ 
            read_lock_release(cache_table[i]);
            return cache_table[i]; // found empty cache slot
        }
    }
    read_lock_release(cache_table[i]);
    return NULL; // no empty cache slot
}

/**
 * evict existing cache: recency > I/O time
 * 
 * 1st loop: flush() occupied by not accsesed, not dirty cache
 * 2nd loop: recency > I/O time: 1st round dirty, 2nd round 2nd chance accessed
 */
bool
clock_algo_evict_cache(block_sector_t disk_addr, void *buffer){
    
    // 1. recency + I/O time: not accsesed, not dirty
    for (size_t i = 0; i < CACHE_SIZE; i++){
        read_lock_acquire(cache_table[i]);
        ASSERT(cache_table[i]->empty = false);
        if(!cache_table[i]->accessed && !cache_table[i]->dirty){
            cache_table[i]->disk_addr = disk_addr;
            cache_table[i]->accessed = true;
            memcpy(buffer, cache_table[i]->cached_page);
            read_lock_release(cache_table[i]);
            return true;
        }
        read_lock_release(cache_table[i]);
    }
    
    // 2. : 1st round dirty, 2nd round 2nd chance accessed
    for (size_t i = 0; i < CACHE_SIZE * 2; i++){
        // 1st round dirty
        if(cache_table[i]->accessed){
            cache_table[i]->accessed = false;
        // 2nd round 2nd chance accessed used
        } else { 
            block_write(); 
            cache_table[i]->disk_addr = disk_addr;
            cache_table[i]->empty = false;     
            cache_table[i]->accessed = true;
            cache_table[i]->dirty = false;       
            memcpy(buffer, cache_table[i]->cached_page);
            return true;
        }
    }
    return false; // something went wrong 
    
}



/**
 * 
 * periodically flush() dirty cache back to disk
 * 
 * 
 */
void thread_func_write_back (void *aux UNUSED)
{
  while (true)
    {
      timer_sleep(WRITE_BACK_INTERVAL);
      filesys_cache_write_to_disk(false);
    }
}


/**
 * 
 * 
 * 
 */ 
void spawn_thread_read_ahead (block_sector_t sector)
{
  block_sector_t *arg = malloc(sizeof(block_sector_t));
  if (arg)
    {
      *arg = sector + 1;
      thread_create("filesys_cache_readahead", 0, thread_func_read_ahead,
      		    arg);
    }
}
/**
 * 
 * 
 * read ahead multiple pages
 */
// interrupt disabled instead of lock ??
void thread_func_read_ahead (void *aux)
{
  block_sector_t sector = * (block_sector_t *) aux;
  lock_acquire(&filesys_cache_lock);
  struct cache_entry *c = block_in_cache(sector);
  if (!c) // cache_block_read() on next block not yet called !!!!!
    {
      filesys_cache_block_evict(sector, false);
    }

    struct thread *child_thread = thread_current();
    sema_up(&child_thread->parent->sema_read_ahead);


  lock_release(&filesys_cache_lock);
  free(aux);
}
