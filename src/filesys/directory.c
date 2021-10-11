/**
 * class scope:
 * - r(), w() dir_entry
 * - deal with dir hierarchy logic
 * 
 * 
 */ 
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
    block_sector_t inode_disk_diskaddr; // inode_disk of file_chunk or dir_entry
    char name[NAME_MAX + 1];            // sub_dir name or file name
    bool in_use;                        /* In use or free? */
  };


static bool
lookup (const struct dir *dir, const char *name,
        struct dir_entry *ep, off_t *ofsp);

 
/***************************************************************/
// API - search
// given full_path_name, return subdir populated with inode_disk







/***************************************************************/

/**
 * chdir, traverse from starting_dir to subdir w path_tokens
 * return dest_dir + file/subdir->inode populated with inode_disk
 * 
 * TODO: 
 * path_tokens[]: e.g. ["home","desktop"] or ["desktop"] if cwd is "home"
 * dir_close() when error
 * removed inode ?? 
 */ 
struct dir*
dir_open_subdir (const struct dir *starting_dir, char *path_tokens[]) 
{
  struct dir_entry e;
  struct dir *dest_dir = malloc(sizeof(struct dir));
  ASSERT (starting_dir != NULL);
  ASSERT (path_tokens != NULL);

  // file/subdir's dir_entry
  if (traverse_inode_disk_till_found_direntry (starting_dir, path_tokens, &e, NULL, dest_dir)){
    return dest_dir;
  }else{
    return NULL;
  }  
}


/**
 * given file/dir name, traverse every lower level dir_entries[] till matching name
 * return whether found, dir_entry n offset
 */ 

static bool
traverse_inode_disk_till_found_direntry (const struct dir *starting_dir, 
        char *path_tokens[], struct dir_entry *ep, off_t *ofsp, struct dir *dest_dir) 
{
  struct dir_entry e;
  size_t ofs;
  
  ASSERT (starting_dir != NULL);
  if(path_tokens == NULL){
      return true; // recursion sucessfully donr
  }

  // 1. for loop dir_entries[] for matching name
  // return whether found, dir_entry n offset
  struct dir *dir = malloc(sizeof(struct dir));
  dir = starting_dir;
  for(size_t i = 0; i > sizeof(path_tokens); i++){
    for (ofs = 0; inode_read_direntry_or_filechunk (dir->inode, &e, sizeof e, ofs) == sizeof e;
        ofs += sizeof e) 
    {
      if (e.in_use && !strcmp (path_tokens[i], e.name)) // found same name 
        {
          if (ep != NULL) *ep = e; // optional return
          if (ofsp != NULL) *ofsp = ofs; // optional return
        }
      return false; // not found 
    }
    if(!dir->inode){
      PANIC("dir_open_subdir() failed, no dir->inode \n");
    }
    if(!dir->inode.inode_disk){
      PANIC("dir_open_subdir() failed, no dir->inode->inode_disk \n");
    }

    // NOTE: empty dir also has inode_disk w dir_entires "." and ".."
    dir = dir_open(inode_open_inode_read_inode_disk (e.inode_disk_diskaddr));
    if(!dir){
      PANIC("dir_open_subdir() failed to open subdir 1 layer deep\n");
    }    
  }
  if(dest_dir != NULL) dest_dir = dir; // optional return
  free(dir);
  return true; // found subdir's direntry
}

// recursion version 
// static bool
// traverse_inode_disk_till_found_direntry (const struct dir *starting_dir, 
//         char *path_tokens[], struct dir_entry *ep, off_t *ofsp) 
// {
//   struct dir_entry e;
//   size_t ofs;
  
//   ASSERT (starting_dir != NULL);
//   if(path_tokens == NULL){
//       return true; // recursion sucessfully donr
//   }

//   // 1. for loop dir_entries[] for matching name
//   // return whether found, dir_entry n offset
//   for (ofs = 0; inode_read_direntry_or_filechunk (starting_dir->inode, &e, sizeof e, ofs) == sizeof e;
//        ofs += sizeof e) 
//   {
//     if (e.in_use && !strcmp (path_tokens[0], e.name)) // found same name 
//       {
//         if (ep != NULL)
//           *ep = e;
//         if (ofsp != NULL)
//           *ofsp = ofs;
//         return true;
//       }

//     // TODO: keep traversing all children dir_entries
//     struct dir *child_dir = get_dir_from_direntry(e);
//     if(!child_dir){ // child is file not dir
//       return false;
//     }
//     if(traverse_inode_disk_till_found_direntry(child_dir, path_tokens[-1], &e, ofs)){
//       return true;
//     }
//   }
//   return false;
// }

/***************************************************************/
// API - add()
// traverse() inode_disk, dir_entries + write() dir_entries






/***************************************************************/
/**
 * aka init_root_dir_inode_disk()
 * called only by format_file_system()
 * 
 * 1. init() inode_disk for 16 dir_entries at disk ROOT_DIR_SECTOR / 1
 * 2. w() 1 dir_entry with "."
 * 
 * entry_cnt: num of dir_entires
 * ?? why current dir "." not parent dir ".." ??
 */
bool
dir_create (block_sector_t sector, size_t entry_cnt)
{
  // 1. init() inode_disk for 16 dir_entries at disk ROOT_DIR_SECTOR / 1
  // + reserve disk space for root_dir dir_entries
  // sector == ROOT_DIR_SECTOR == 1, entry_cnt == 16
  if(inode_create_inode_disk_reserve_disk (sector, entry_cnt * sizeof (struct dir_entry))){
    PANIC("dir_create() failed to init() root_dir inode_disk\n");
  }
  
  // TODO -> switch from 1 dir to sub_dirs
  // 2. w() 1 dir_entry with "."
  struct dir *d = dir_open(inode_open_inode_read_inode_disk (sector));
  if(dir_add_direntry(d, ".", sector)){
    PANIC("dir_create() adding . current dir to root_dir inode_disk failed \n");
  }
}

/**
 * w() filename to 1st dir_entry available
 * 
 * inode_sector: inode_disk created from inode_create_inode_disk_reserve_disk()
 * 
 * TODO: 
 * add is_dir ?? NO, is_dir is inode_disk{}
 * whether need to w() inode_disk ??
 */ 
bool
dir_add_direntry (struct dir *dest_dir, const char *name, block_sector_t inode_sector)
{
  struct dir_entry e;
  off_t ofs;
  bool success = false;

  ASSERT (dest_dir != NULL);
  ASSERT (name != NULL);

  if (*name == '\0' || strlen (name) > NAME_MAX) // valid name
    return false;
  // name not already in use
  if (traverse_inode_disk_till_found_direntry (dest_dir, name, NULL, NULL, NULL)) 
    goto done;

  // 1. for loop same level dir_entries[], find 1st free
  for (ofs = 0; inode_read_direntry_or_filechunk (dest_dir->inode, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e) 
    if (!e.in_use)
      break;

  // 2. w() dir_entry w filename
  e.in_use = true;
  strlcpy (e.name, name, sizeof e.name);
  e.inode_disk_diskaddr = inode_sector; // already has isdir
  success = inode_write_direntry_or_filechunk (dest_dir->inode, &e, sizeof e, ofs) == sizeof e;

 done:
  return success;
}





/***************************************************************/
// API - delete()





/***************************************************************/
/**
 * 
 * 
 * 
 */ 
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

  inode = inode_open_inode_read_inode_disk (e.inode_sector);
  if (inode == NULL)
    goto done;

  e.in_use = false;
  if (inode_write_direntry_or_filechunk (dir->inode, &e, sizeof e, ofs) != sizeof e) 
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

  while (inode_read_direntry_or_filechunk (dir->inode, &e, sizeof e, dir->pos) == sizeof e) 
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
  return dir_open (inode_open_inode_read_inode_disk (ROOT_DIR_SECTOR));
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


/**
 * malloc() dir, populate w inode
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