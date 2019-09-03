#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"

/* Partition that contains the file system. */
struct block *fs_device;
static void do_format (void);

/***************************************************************/
// APIs 












/***************************************************************/


// creates an empty file or sub_dir 
bool
filesys_create (const char *name, off_t initial_size) 
{
  block_sector_t inode_sector = 0;
  printf("filesys.c filesys_create() 1 \n");
  struct dir *dir = dir_open_root ();
  printf("filesys.c filesys_create() 2 \n");
  bool success = (dir != NULL
                  && free_map_allocate (1, &inode_sector)
                  && inode_create (inode_sector, initial_size)
                  && dir_add (dir, name, inode_sector));
  printf("filesys.c filesys_create() 3 \n");
  if (!success && inode_sector != 0){
    printf("filesys.c filesys_create() 4 \n");
    free_map_release (inode_sector, 1);
  }
  dir_close (dir);

  return success;
}



// given file path, traverse hard disk, return file
struct file *
filesys_open (const char *name)
{
  struct dir *dir = dir_open_root ();
  struct inode *inode = NULL;

  if (dir != NULL)
    dir_lookup (dir, name, &inode);
  dir_close (dir);

  return file_open (inode);
}



bool
filesys_remove (const char *name) 
{
  struct dir *dir = dir_open_root ();
  bool success = dir != NULL && dir_remove (dir, name);
  dir_close (dir); 

  return success;
}



/***************************************************************/
// helpers 












/***************************************************************/


static void
do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, 16))
    PANIC ("root directory creation failed");
  free_map_close ();
  printf ("done.\n");
}


void
filesys_init (bool format) 
{
  fs_device = block_get_role (BLOCK_FILESYS);
  if (fs_device == NULL)
    PANIC ("No file system device found, can't initialize file system.");

  inode_init ();
  free_map_init ();
  // TODO: FS init() buffer cache

  if (format) 
    do_format ();

  free_map_open ();
}


void
filesys_done (void) 
{
  free_map_close ();
}

