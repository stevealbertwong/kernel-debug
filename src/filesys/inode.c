#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include <stdio.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"

#define INODE_MAGIC 0x494e4f44  /* Identifies an inode. */

// must be BLOCK_SECTOR_SIZE(512) bytes -> 125 * 4 + 12
struct inode_disk // inode (on hard disk) -> for both file / dir
  {
    block_sector_t start;               /* First data sector. */
    off_t length;                       // sizeof(dir_entry) or file size
    unsigned magic;                     /* Magic number. */
    uint32_t unused[125];               // 500 bytes
  };


struct inode // inode (in memory)
  {
    struct list_elem elem;              /* Element in inode list. */
    block_sector_t sector;              // disk addr of inode_disk
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    struct inode_disk data;             /* Inode content. */
  };

static struct list open_inodes;

static block_sector_t byte_to_sector (const struct inode *inode, off_t pos); 
static inline size_t bytes_to_sectors (off_t size);
/***************************************************************/
// key helpers












/***************************************************************/

// TODO
static block_sector_t
byte_to_sector (const struct inode *inode, off_t pos) 
{
  ASSERT (inode != NULL);
  if (pos < inode->data.length) // pos == where to start r()/w()
    // return disk sector addr 
    return inode->data.start + pos / BLOCK_SECTOR_SIZE;
  else // pos exceeds inode_disk's file size
    return -1;
}

/***************************************************************/
// key APIs 












/***************************************************************/

/**
 * read() at inode_disk->direct_block[] w offset, size/PAGE_SIZE times, into buffer
 * 
 * 
 * 
 */ 
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) 
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  uint8_t *bounce = NULL;

  while (size > 0) 
    {
      // TODO
      // from Direct Indirect Block, where in disk to read()
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      // bytes left in inode_disk->size, bytes left in sector -> lesser of the 2
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      // chunk_size == num of bytes actually written into this sector
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          // TODO
          block_read (fs_device, sector_idx, buffer + bytes_read);
        }
      else 
        {
          if (bounce == NULL) 
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }
          block_read (fs_device, sector_idx, bounce);
          memcpy (buffer + bytes_read, bounce + sector_ofs, chunk_size);
        }
      // advance
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }
  free (bounce);

  return bytes_read;
}

/**
 * write data into disk indexed() by inode_disk
 *    inode_disk ALREADY EXISTED
 *    e.g. filesys_create() == inode_create() + inode_write_at()
 * 
 * 
 * 
 * 
 */ 
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset) 
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
  uint8_t *bounce = NULL;

  if (inode->deny_write_cnt){
    // printf("inode.c inode->deny_write_cnt \n");
    return 0;
  }


  // TODO -> if beyond EOF, implements file growth


  while (size > 0) 
    {
      // TODO
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          // TODO
          block_write (fs_device, sector_idx, buffer + bytes_written);
        }
      else 
        {
          if (bounce == NULL) 
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }
          if (sector_ofs > 0 || chunk_size < sector_left) 
            // TODO
            block_read (fs_device, sector_idx, bounce);
          else
            memset (bounce, 0, BLOCK_SECTOR_SIZE);
          memcpy (bounce + sector_ofs, buffer + bytes_written, chunk_size);
          block_write (fs_device, sector_idx, bounce);
        }

      // advance
      // printf("inode.c inode_write_at() chunk_size %d \n", chunk_size);
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }
  free (bounce);

  return bytes_written;
}


/**
 * create inode_disk(dir_entry/file_chunks)in disk + reserve disk space for dir_entry/file_chunks)
 * 
 * 1. calloc() + populate() inode_disk
 * 2. w() inode_disk (dir_entry/file_chunks)
 * 3. w() 0s to reserve space for dir_entry/file_chunks
 */
bool
inode_create (block_sector_t sector, off_t length)
{
  struct inode_disk *disk_inode = NULL;
  bool success = false;
  ASSERT (length >= 0);
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE); // inode == 1 sector in size

  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
    {
      size_t sectors = bytes_to_sectors (length);
      disk_inode->length = length;
      disk_inode->magic = INODE_MAGIC;
      
      // TODO -> direct, indirect
      if (free_map_allocate (sectors, &disk_inode->start))
        {
          // 2. w() inode_disk (dir_entry/file_chunks)
          block_write (fs_device, sector, disk_inode);
          if (sectors > 0) 
            {
              static char zeros[BLOCK_SECTOR_SIZE];
              size_t i;
              // 3. w() 0s to reserve space for dir_entry/file_chunks
              for (i = 0; i < sectors; i++) 
                block_write (fs_device, disk_inode->start + i, zeros); // TODO
            }
          success = true; 
        } 
      free (disk_inode);
    }
  return success;
}


/**
 * 
 * assuming sector/disk addr storing inode_disk
 * 
 * 1. check if inode cache() in open list (i.e. inode_cache) first
 * 2. if not inode_cache, check page_cache
 * 3. if not, read() inode_disk from disk
 */ 
struct inode *
inode_open (block_sector_t sector)
{
  struct list_elem *e;
  struct inode *inode;
  // 1. check if inode cache() in open list (i.e. inode_cache) first
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e)) 
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector) 
        {
          inode_reopen (inode);
          return inode; 
        }
    }

  // 2. if not inode_cache, check page_cache -> TODO
  // 3. if not, read() inode_disk from disk 
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;

  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  block_read (fs_device, inode->sector, &inode->data);
  return inode;
}



/**
 * 
 * 
 * 
 * 
 */ 
void
inode_close (struct inode *inode) 
{
  if (inode == NULL)
    return;
  if (--inode->open_cnt == 0)
    {
      list_remove (&inode->elem);
      if (inode->removed) 
        {
          free_map_release (inode->sector, 1);
          // TODO
          free_map_release (inode->data.start,
                            bytes_to_sectors (inode->data.length)); 
        }
      free (inode); 
    }
}





/***************************************************************/
// helpers
















/***************************************************************/


static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

void
inode_init (void) 
{
  list_init (&open_inodes);
}

void
inode_deny_write (struct inode *inode) 
{
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

void
inode_allow_write (struct inode *inode) 
{
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

off_t
inode_length (const struct inode *inode)
{
  return inode->data.length;
}

struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    inode->open_cnt++;
  return inode;
}

block_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

void
inode_remove (struct inode *inode) 
{
  ASSERT (inode != NULL);
  inode->removed = true;
}