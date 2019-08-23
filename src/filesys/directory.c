#include "filesys/directory.h"
#include <stdio.h>
#include <string.h>
#include <list.h>
#include "filesys/filesys.h"
#include "filesys/inode.h"
#include "threads/malloc.h"

struct dir // memory
  {
    struct inode *inode;                /* Backing store. */
    off_t pos;                          /* Current position. */
  };

struct dir_entry // disk
  {
    block_sector_t inode_sector;        // disk addr of inode_disk / dir_entry
    char name[NAME_MAX + 1];            // sub_dir name or file name
    bool in_use;                        /* In use or free? */
  };

/***************************************************************/
// APIs - traverse() inode_disk, dir_entries












/***************************************************************/

/**
 * traverse() inode_disk
 * 
 * return dir populated with path's data from hard disk
 * 1. 
 * 
 * 
 */ 

// TODO: dir_open_path()




/**
 * traverse() inode_disk
 * 
 * 
 * 
 * 
 */ 
bool
dir_lookup (const struct dir *dir, const char *name,
            struct inode **inode) 
{
  struct dir_entry e;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  if (lookup (dir, name, &e, NULL))
    *inode = inode_open (e.inode_sector);
  else
    *inode = NULL;

  return *inode != NULL;
}


/**
 * traverse() inode_disk
 * 
 * 
 * 
 * 
 */ 
static bool
lookup (const struct dir *dir, const char *name,
        struct dir_entry *ep, off_t *ofsp) 
{
  struct dir_entry e;
  size_t ofs;
  
  ASSERT (dir != NULL);
  ASSERT (name != NULL);
  // for loop traverse() dir_entries -> found matching dir/filename -> return dir_entry n offset
  for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e) 
    if (e.in_use && !strcmp (name, e.name)) 
      {
        if (ep != NULL)
          *ep = e;
        if (ofsp != NULL)
          *ofsp = ofs;
        return true;
      }
  return false;
}


/**
 * 
 * 
 * 
 * 
 * 
 */ 
struct dir *
dir_open (struct inode *inode) 
{
  struct dir *dir = calloc (1, sizeof *dir);
  if (inode != NULL && dir != NULL)
    {
      dir->inode = inode;
      dir->pos = 0;
      return dir;
    }
  else
    {
      inode_close (inode);
      free (dir);
      return NULL; 
    }
}





/***************************************************************/
// APIs - traverse() inode_disk, dir_entries + write() dir_entries












/***************************************************************/


/**
 * aka init_root_dir()
 * 
 * 1. init() inode_disk for 16 dir_entries at disk ROOT_DIR_SECTOR / 1
 * 2. w() 1 dir_entry with "."
 */
bool
dir_create (block_sector_t sector, size_t entry_cnt)
{
  // 1. init() inode_disk for 16 dir_entries at disk ROOT_DIR_SECTOR / 1
  // sector == ROOT_DIR_SECTOR == 1, entry_cnt == 16
  return inode_create (sector, entry_cnt * sizeof (struct dir_entry));
  
  // TODO -> switch from 1 dir to sub_dirs
  // 2. w() 1 dir_entry with "."









}




/**
 * 
 * inode_sector: inode_disk created from inode_create()
 * 
 */ 
bool
dir_add (struct dir *dir, const char *name, block_sector_t inode_sector)
{
  struct dir_entry e;
  off_t ofs;
  bool success = false;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  /* Check NAME for validity. */
  if (*name == '\0' || strlen (name) > NAME_MAX)
    return false;

  /* Check that NAME is not in use. */
  if (lookup (dir, name, NULL, NULL))
    goto done;

  /* Set OFS to offset of free slot.
     If there are no free slots, then it will be set to the
     current end-of-file.
     
     inode_read_at() will only return a short read at end of file.
     Otherwise, we'd need to verify that we didn't get a short
     read due to something intermittent such as low memory. */
  for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e) 
    if (!e.in_use)
      break;

  /* Write slot. */
  e.in_use = true;
  strlcpy (e.name, name, sizeof e.name);
  e.inode_sector = inode_sector;
  success = inode_write_at (dir->inode, &e, sizeof e, ofs) == sizeof e;

 done:
  return success;
}






bool
dir_remove (struct dir *dir, const char *name) 
{
  struct dir_entry e;
  struct inode *inode = NULL;
  bool success = false;
  off_t ofs;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  if (!lookup (dir, name, &e, &ofs))
    goto done;

  inode = inode_open (e.inode_sector);
  if (inode == NULL)
    goto done;

  e.in_use = false;
  if (inode_write_at (dir->inode, &e, sizeof e, ofs) != sizeof e) 
    goto done;

  inode_remove (inode);
  success = true;

 done:
  inode_close (inode);
  return success;
}




bool
dir_readdir (struct dir *dir, char name[NAME_MAX + 1])
{
  struct dir_entry e;

  while (inode_read_at (dir->inode, &e, sizeof e, dir->pos) == sizeof e) 
    {
      dir->pos += sizeof e;
      if (e.in_use)
        {
          strlcpy (name, e.name, NAME_MAX + 1);
          return true;
        } 
    }
  return false;
}

/***************************************************************/
// helpers












/***************************************************************/


struct dir *
dir_open_root (void)
{
  return dir_open (inode_open (ROOT_DIR_SECTOR));
}


struct dir *
dir_reopen (struct dir *dir) 
{
  return dir_open (inode_reopen (dir->inode));
}


void
dir_close (struct dir *dir) 
{
  if (dir != NULL)
    {
      inode_close (dir->inode);
      free (dir);
    }
}

struct inode *
dir_get_inode (struct dir *dir) 
{
  return dir->inode;
}