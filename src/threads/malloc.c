#include "threads/malloc.h"
#include <debug.h>
#include <list.h>
#include <round.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/vaddr.h"

#define ARENA_MAGIC 0x9a548eed // Magic number for detecting arena corruption
static size_t block_size (void *block);


// global malloc table
// desc == 1 type of memory size unit e.g. 32 byte
struct desc 
  {
    size_t block_size;          /* Size of each element in bytes. */
    size_t blocks_per_arena;    /* Number of blocks in an arena. */
    struct list free_list;      // ALL free BLOCKS(mini memory unit) form ALL PAGES
    struct lock lock;           // protect u() free_list[], free_cnt
  };

// block == mini memory unit within a page e.g. 4KB = 32 bytes * 120 memory unit
struct block 
  {
    struct list_elem free_elem; // belong to desc->free_list
  };

// arena == header of 1 page i.e. 1 page memory = 1 struct arena + many blocks
// free() page that has no malloc() memory unit left w free_cnt
struct arena 
  {
    struct desc *desc;          // memory unit type
    size_t free_cnt;            // free mini memory unit left within this page
    unsigned magic;             // user could corrupt malloc() memory
  };

static struct desc descs[10];   // all descriptors
static size_t desc_cnt;         // might not use up all 10 

static struct arena *block_to_arena (struct block *);
static struct block *arena_to_block (struct arena *, size_t idx);


/***************************************************************/
// key APIs












/***************************************************************/

// init() descriptors in kernel stack
void
malloc_init (void) 
{
  size_t block_size;
  // 16,32,64 ... 2048 bytes, 10 descs types, each *2
  for (block_size = 16; block_size < PGSIZE / 2; block_size *= 2)
    {
      struct desc *d = &descs[desc_cnt++];
      ASSERT (desc_cnt <= sizeof descs / sizeof *descs);
      d->block_size = block_size;
      d->blocks_per_arena = (PGSIZE - sizeof (struct arena)) / block_size;
      list_init (&d->free_list);
      lock_init (&d->lock);
    }
}

/**
 * 1. find smallest descriptor
 *    1.1 if more than 2KB, palloc_get_multiple() instead
 * 2. find free memory unit 
 *    2.1 if page is full, palloc() new page, init() with header "arena"
 */ 
void *
malloc (size_t size) 
{
  struct desc *d;
  struct block *b;
  struct arena *a;

  if (size == 0)
    return NULL;

  // 1. find smallest descriptor
  // for loop smallest descriptor that satisfies a SIZE-byte request  
  for (d = descs; d < descs + desc_cnt; d++)
    if (d->block_size >= size)
      break;
  
  // 1.1 if more than 2KB, palloc_get_multiple() instead
  // reach end of desciptors[], size is too big for any descriptor
  if (d == descs + desc_cnt) 
    {
      // palloc() enough pages to hold SIZE plus an arena. */
      size_t page_cnt = DIV_ROUND_UP (size + sizeof *a, PGSIZE);
      a = palloc_get_multiple (0, page_cnt);
      if (a == NULL)
        return NULL;

      // init() arena header, return addr after arena header
      a->magic = ARENA_MAGIC;
      a->desc = NULL;
      a->free_cnt = page_cnt;
      return a + 1;
    }

  // 2. find free memory unit 
  lock_acquire (&d->lock);
  // 2.1 if page is full, palloc() new page, init() with header "arena"
  if (list_empty (&d->free_list))
    {
      size_t i;
      a = palloc_get_page (0);
      if (a == NULL) 
        {
          lock_release (&d->lock);
          return NULL; 
        }
      // init() arena + add() memory units to free list
      a->magic = ARENA_MAGIC;
      a->desc = d;
      a->free_cnt = d->blocks_per_arena;
      for (i = 0; i < d->blocks_per_arena; i++) 
        {
          struct block *b = arena_to_block (a, i);
          list_push_back (&d->free_list, &b->free_elem);
        }
    }

  // pop() memory unit from free list
  b = list_entry (list_pop_front (&d->free_list), struct block, free_elem);
  a = block_to_arena (b);
  a->free_cnt--;
  lock_release (&d->lock);
  return b;
}



/**
 * 1. 
 * 2. 
 * 
 * 
 * 
 */ 
/* Frees block P, which must have been previously allocated with
   malloc(), calloc(), or realloc(). */
void
free (void *p) 
{
  if (p != NULL)
    {
      struct block *b = p;
      struct arena *a = block_to_arena (b);
      struct desc *d = a->desc;
      
      if (d != NULL) 
        {
          /* It's a normal block.  We handle it here. */

#ifndef NDEBUG
          /* Clear the block to help detect use-after-free bugs. */
          memset (b, 0xcc, d->block_size);
#endif
  
          lock_acquire (&d->lock);

          /* Add block to free list. */
          list_push_front (&d->free_list, &b->free_elem);

          /* If the arena is now entirely unused, free it. */
          if (++a->free_cnt >= d->blocks_per_arena) 
            {
              size_t i;

              ASSERT (a->free_cnt == d->blocks_per_arena);
              for (i = 0; i < d->blocks_per_arena; i++) 
                {
                  struct block *b = arena_to_block (a, i);
                  list_remove (&b->free_elem);
                }
              palloc_free_page (a);
            }

          lock_release (&d->lock);
        }
      else
        {
          /* It's a big block.  Free its pages. */
          palloc_free_multiple (a, a->free_cnt);
          return;
        }
    }
}


/***************************************************************/
// helpers












/***************************************************************/


// malloc A times B bytes initialized to zeroes
void *
calloc (size_t a, size_t b) 
{
  void *p;
  size_t size;

  /* Calculate block size and make sure it fits in size_t. */
  size = a * b;
  if (size < a || size < b)
    return NULL;

  /* Allocate and zero memory. */
  p = malloc (size);
  if (p != NULL)
    memset (p, 0, size);

  return p;
}


/* Attempts to resize OLD_BLOCK to NEW_SIZE bytes, possibly
   moving it in the process.
   If successful, returns the new block; on failure, returns a
   null pointer.
   A call with null OLD_BLOCK is equivalent to malloc(NEW_SIZE).
   A call with zero NEW_SIZE is equivalent to free(OLD_BLOCK). */
void *
realloc (void *old_block, size_t new_size) 
{
  if (new_size == 0) 
    {
      free (old_block);
      return NULL;
    }
  else 
    {
      void *new_block = malloc (new_size);
      if (old_block != NULL && new_block != NULL)
        {
          size_t old_size = block_size (old_block);
          size_t min_size = new_size < old_size ? new_size : old_size;
          memcpy (new_block, old_block, min_size);
          free (old_block);
        }
      return new_block;
    }
}


static size_t
block_size (void *block) 
{
  struct block *b = block;
  struct arena *a = block_to_arena (b);
  struct desc *d = a->desc;

  return d != NULL ? d->block_size : PGSIZE * a->free_cnt - pg_ofs (block);
}

/* Returns the arena that block B is inside. */
static struct arena *
block_to_arena (struct block *b)
{
  struct arena *a = pg_round_down (b);

  /* Check that the arena is valid. */
  ASSERT (a != NULL);
  ASSERT (a->magic == ARENA_MAGIC);

  /* Check that the block is properly aligned for the arena. */
  ASSERT (a->desc == NULL
          || (pg_ofs (b) - sizeof *a) % a->desc->block_size == 0);
  ASSERT (a->desc != NULL || pg_ofs (b) == sizeof *a);

  return a;
}

/* Returns the (IDX - 1)'th block within arena A. */
static struct block *
arena_to_block (struct arena *a, size_t idx) 
{
  ASSERT (a != NULL);
  ASSERT (a->magic == ARENA_MAGIC);
  ASSERT (idx < a->desc->blocks_per_arena);
  return (struct block *) ((uint8_t *) a
                           + sizeof *a
                           + idx * a->desc->block_size);
}
