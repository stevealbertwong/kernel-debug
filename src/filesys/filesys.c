/**
 * class scope:
 * - dir
 * - file
 * - inode
 * - map of free disk space for inode_disk
 */  
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
// APIs - add(), search(), delete() subdir/file






/***************************************************************/
/**
 * add an EMPTY file/sub_dir (empty == no file chunk)
 * called by syscall_create(), syscall_mkdir()
 * 
 * e.g. creates 2.txt, 3.dir in /home/desktop
 * 
 * 1. parse path e.g. ["home","desktop"] or ["desktop"] if cwd is "home"
 * 2. open subdir{} populated with inode_disk{} e.g. "desktop"
 * 3. w() new inode_disk + reserve disk space  e.g. 2.txt, 3.dir 
 * 4. w() new dir_entry, attach new inode_disk to new dir_entry
 */ 
bool
filesys_create(const char *full_path, off_t initial_size, bool isdir) 
{
  block_sector_t free_disk_addr_for_inode_disk = 0;  
  struct inode *subdir_inode;

  // TODO: relative path handling
  char *full_path_tokens[] = parse_pathname(full_path);
  char *name = full_path_tokens[sizeof(full_path_tokens)];//dir or file
  
  struct dir *starting_dir = thread_current()->current_working_dir;
  struct dir *rootdir = dir_open_root ();
  if(!rootdir){
    PANIC("filesys_create() failed to get rootdir \n");
  }
  
  // contains only from starting dir to subdir
  char *subdir_path_tokens[]; // e.g. ["home","desktop"] or ["desktop"] if cwd is "home"
  char *new_file_name;
  char *new_subdir_name;

  // 2. "home" + "desktop"
  // "desktop" subdir->subdir_inode->inode_disk (dir_entries disk addr)
  struct dir *subdir = dir_open_subdir(starting_dir, subdir_path_tokens);
  if(!subdir_inode || !subdir_inode->inode_disk){
    PANIC("filesys_create() failed to found file in full_path\n");
    return -1;
  }  

  // 3. "2.txt" or "3.subdir"
  // w() 3.subdir inode_disk + w() 3.subdir dir_entry + add ".." in 3.subdir
  bool success = (free_map_allocate (1, &free_disk_addr_for_inode_disk)
                  // new 2.txt or 3.dir's inode_disk + reserve disk space for 2.txt
                  && inode_create_inode_disk_reserve_disk (free_disk_addr_for_inode_disk, initial_size, isdir)
                  // w() new dir_entry + attach new inode_disk to new dir_entry
                  && dir_add_direntry (subdir, name, free_disk_addr_for_inode_disk));
  if(isdir){ // add ".." in 3.subdir
    struct dir *new_subdir = dir_open_subdir(subdir, new_subdir_name);
    dir_add_direntry (subdir, "..", free_disk_addr_for_inode_disk); // parent inode_disk addr
  }

  if (!success && free_disk_addr_for_inode_disk != 0){
    free_map_release (free_disk_addr_for_inode_disk, 1);
  }
  
  free(full_path_tokens);
  dir_close (subdir);
  return success;
}


/**
 * given file path, traverse hard disk, return file
 * 
 * 
 */ 
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

bool
parse_full_path(const void *full_path_name){

}