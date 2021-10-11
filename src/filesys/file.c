/**
 * class scope:
 * - r() w() file_chunk from disk
 * 
 * 
 */ 
#include "filesys/file.h"
#include <debug.h>
#include "filesys/inode.h"
#include "threads/malloc.h"

struct file 
  {
    struct inode *inode;        /* File's inode. */
    off_t pos;                  /* Current position. */
    bool deny_write;            /* Has file_deny_write() been called? */
  };


/***************************************************************/
// APIs






/***************************************************************/

off_t
file_read (struct file *file, void *buffer, off_t size) 
{
  off_t bytes_read = inode_read_direntry_or_filechunk (file->inode, buffer, size, file->pos);
  file->pos += bytes_read;
  return bytes_read;
}

off_t
file_read_at (struct file *file, void *buffer, off_t size, off_t file_ofs) 
{
  return inode_read_direntry_or_filechunk (file->inode, buffer, size, file_ofs);
}

off_t
file_write (struct file *file, const void *buffer, off_t size) 
{
  off_t bytes_written = inode_write_direntry_or_filechunk (file->inode, buffer, size, file->pos);
  file->pos += bytes_written;
  return bytes_written;
}


off_t
file_write_at (struct file *file, const void *buffer, off_t size,
               off_t file_ofs) 
{
  return inode_write_direntry_or_filechunk (file->inode, buffer, size, file_ofs);
}



struct file *
file_open (struct inode *inode) 
{
  struct file *file = calloc (1, sizeof *file);
  if (inode != NULL && file != NULL)
    {
      file->inode = inode;
      file->pos = 0;
      file->deny_write = false;
      return file;
    }
  else
    {
      inode_close (inode);
      free (file);
      return NULL; 
    }
}

void
file_close (struct file *file) 
{
  if (file != NULL)
    {
      file_allow_write (file);
      inode_close (file->inode);
      free (file); 
    }
}

struct file *
file_reopen (struct file *file) 
{
  return file_open (inode_reopen (file->inode));
}

/***************************************************************/
// helpers






/***************************************************************/
void
file_deny_write (struct file *file) 
{
  ASSERT (file != NULL);
  if (!file->deny_write) 
    {
      file->deny_write = true;
      inode_deny_write (file->inode);
    }
}

void
file_allow_write (struct file *file) 
{
  ASSERT (file != NULL);
  if (file->deny_write) 
    {
      file->deny_write = false;
      inode_allow_write (file->inode);
    }
}

/* Returns the size of FILE in bytes. */
off_t
file_length (struct file *file) 
{
  ASSERT (file != NULL);
  return inode_length (file->inode);
}

/* Sets the current position in FILE to NEW_POS bytes from the
   start of the file. */
void
file_seek (struct file *file, off_t new_pos)
{
  ASSERT (file != NULL);
  ASSERT (new_pos >= 0);
  file->pos = new_pos;
}

/* Returns the current position in FILE as a byte offset from the
   start of the file. */
off_t
file_tell (struct file *file) 
{
  ASSERT (file != NULL);
  return file->pos;
}


struct inode *
file_get_inode (struct file *file) 
{
  return file->inode;
}