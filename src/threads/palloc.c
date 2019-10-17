#include "threads/palloc.h"
#include <bitmap.h>
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "threads/loader.h"
#include "threads/synch.h"
#include "threads/vaddr.h"

struct pool // memory pool -> two pools: one for kernel data, one for user pages
  {
    struct lock lock;                   /* Mutual exclusion. */
    struct bitmap *used_map;            // Bitmap of free pages
    uint8_t *base;                      // starting memory of free pages
  };

static struct pool kernel_pool, user_pool;
static void init_pool (struct pool *, void *base, size_t page_cnt,
                       const char *name);
static bool page_from_pool (const struct pool *, void *page);

/**
 * init() free pages for user + kernel 
 */
void
palloc_init (size_t user_page_limit)
{
  // RAM for user starts at 1 MB and runs to the end of RAM 
  uint8_t *free_start = ptov (1024 * 1024); // VA = PA + PHYS_BASE = 1MB + 3GB 
  uint8_t *free_end = ptov (init_ram_pages * PGSIZE);
  size_t free_pages = (free_end - free_start) / PGSIZE; // total no. pages
  
  // half free pages to user, half to kernel
  size_t user_pages = free_pages / 2; // 50% to user page  
  if (user_pages > user_page_limit) // at most USER_PAGE_LIMIT pages are put into user pool
    user_pages = user_page_limit;
  size_t kernel_pages = free_pages - user_pages; // 50% to kernel
  
  // init() struct pool
  init_pool (&kernel_pool, free_start, kernel_pages, "kernel pool");
  init_pool (&user_pool, free_start + kernel_pages * PGSIZE,
             user_pages, "user pool");
}

static void
init_pool (struct pool *p, void *base, size_t page_cnt, const char *name) 
{  
  // pages for bitmap
  size_t bm_pages = DIV_ROUND_UP (bitmap_buf_size (page_cnt), PGSIZE); 
  if (bm_pages > page_cnt)
    PANIC ("Not enough memory in %s for bitmap.", name);
  page_cnt -= bm_pages;
  printf ("%zu pages available in %s.\n", page_cnt, name);

  lock_init (&p->lock);

  // pool's bitmap placed at the start of memory pool
  p->used_map = bitmap_create_in_buf (page_cnt, base, bm_pages * PGSIZE);
  p->base = base + bm_pages * PGSIZE; // base starts after pages allocated for bitmap
}


/**
 * get(), free() pages
 */
// kernel pool: 3-3.5GB, user pool: 3.5-4GB
void *
palloc_get_page (enum palloc_flags flags) 
{
  return palloc_get_multiple (flags, 1);
}

void *
palloc_get_multiple (enum palloc_flags flags, size_t page_cnt)
{
  // flag: default kernel, unless specify PAL_USER then user 
  struct pool *pool = flags & PAL_USER ? &user_pool : &kernel_pool;
  void *pages;
  size_t page_idx;

  if (page_cnt == 0)
    return NULL;

  lock_acquire (&pool->lock);
  page_idx = bitmap_scan_and_flip (pool->used_map, 0, page_cnt, false);
  lock_release (&pool->lock);

  if (page_idx != BITMAP_ERROR)
    pages = pool->base + PGSIZE * page_idx;
  else
    pages = NULL;

  if (pages != NULL) 
    {
      if (flags & PAL_ZERO)
        memset (pages, 0, PGSIZE * page_cnt);
    }
  else 
    {
      if (flags & PAL_ASSERT)
        PANIC ("palloc_get: out of pages");
    }

  return pages;
}

void
palloc_free_page (void *page) 
{
  // printf("palloc_free_page() called \n");
  palloc_free_multiple (page, 1);
}


void
palloc_free_multiple (void *pages, size_t page_cnt) 
{
  // printf("palloc_free_multiple() called, size: %d\n", page_cnt);
  struct pool *pool;
  size_t page_idx;
  ASSERT (pg_ofs (pages) == 0);
  if (pages == NULL || page_cnt == 0)
    return;

  if (page_from_pool (&kernel_pool, pages))
    pool = &kernel_pool;
  else if (page_from_pool (&user_pool, pages))
    pool = &user_pool;
  else
    NOT_REACHED ();

  page_idx = pg_no (pages) - pg_no (pool->base);

#ifndef NDEBUG
  memset (pages, 0xcc, PGSIZE * page_cnt);
#endif

  // bitmap_all() == every bit is true
  ASSERT (bitmap_all (pool->used_map, page_idx, page_cnt));
  bitmap_set_multiple (pool->used_map, page_idx, page_cnt, false);
}



// check if page is from user / kernel pool
static bool
page_from_pool (const struct pool *pool, void *page) 
{
  size_t page_no = pg_no (page);
  size_t start_page = pg_no (pool->base);
  size_t end_page = start_page + bitmap_size (pool->used_map);

  return page_no >= start_page && page_no < end_page;
}
