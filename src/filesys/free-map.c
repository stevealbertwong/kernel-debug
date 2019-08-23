#include "filesys/free-map.h"
#include <bitmap.h>
#include <debug.h>
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "filesys/inode.h"

static struct file *free_map_file;   /* Free map file. */
static struct bitmap *free_map;      /* Free map, one bit per sector. */

/***************************************************************/
// key API












/***************************************************************/

/**
 * check free space in disk to store dir_entry{} / inode_disk{} / data
 * -> contagious blocks as long as free
 */ 
bool
free_map_allocate (size_t cnt, block_sector_t *sectorp)
{
  // look for cnt consecutive sectors in disk (1 bit == 1 disk sector 512B)
  block_sector_t sector = bitmap_scan_and_flip (free_map, 0, cnt, false);
  if (sector != BITMAP_ERROR
      && free_map_file != NULL
      && !bitmap_write (free_map, free_map_file))
    {
      bitmap_set_multiple (free_map, sector, cnt, false); 
      sector = BITMAP_ERROR;
    }
  if (sector != BITMAP_ERROR)
    *sectorp = sector; // return free space in disk using *sectorp !!!!
  return sector != BITMAP_ERROR;
}

/***************************************************************/
// other APIs 














/***************************************************************/

// called only once when init filesystem
void
free_map_init (void) 
{
  free_map = bitmap_create (block_size (fs_device));
  if (free_map == NULL)
    PANIC ("bitmap creation failed--file system device is too large");
  bitmap_mark (free_map, FREE_MAP_SECTOR);
  bitmap_mark (free_map, ROOT_DIR_SECTOR);
}

// called only once when format disk
void
free_map_create (void) 
{
  if (!inode_create (FREE_MAP_SECTOR, bitmap_file_size (free_map)))
    PANIC ("free map creation failed");

  free_map_file = file_open (inode_open (FREE_MAP_SECTOR));
  if (free_map_file == NULL)
    PANIC ("can't open free map");
  if (!bitmap_write (free_map, free_map_file))
    PANIC ("can't write free map");
}

void
free_map_release (block_sector_t sector, size_t cnt)
{
  ASSERT (bitmap_all (free_map, sector, cnt));
  bitmap_set_multiple (free_map, sector, cnt, false);
  bitmap_write (free_map, free_map_file);
}

void
free_map_open (void) 
{
  free_map_file = file_open (inode_open (FREE_MAP_SECTOR));
  if (free_map_file == NULL)
    PANIC ("can't open free map");
  if (!bitmap_read (free_map, free_map_file))
    PANIC ("can't read free map");
}

void
free_map_close (void) 
{
  file_close (free_map_file);
}

