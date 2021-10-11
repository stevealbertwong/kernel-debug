#ifndef FILESYS_INODE_H
#define FILESYS_INODE_H

#include <stdbool.h>
#include "filesys/off_t.h"
#include "devices/block.h"

#define INODE_MAGIC 0x494e4f44  /* Identifies an inode. 4 bytes == 8 4bits */

/**
 * must be BLOCK_SECTOR_SIZE(512) bytes:
 * 500 + 1 + 8 + 4 = 513 bytes, magic only reads 3 bytes instead of 4 ??  
 */ 
struct inode_disk // inode (on hard disk) -> index both file / dir
  {
    // uint32_t unused[125];               // 125 * 4 = 500 bytes    
    // inode_disk == disk address == block_sector_t =/= pointer, cannot be dereference !!!!!!
    block_sector_t direct_disk_addrs[123]; // 128 disk addrs of dir_entries/file_chunks
    block_sector_t one_layer_inode_disk;   // 1 addr of struct first_layer_inode_disk in disk
    block_sector_t two_layer_inode_disk;   // 1 addr of struct second_layer_inode_disk in disk

    // https://stackoverflow.com/questions/4626815/why-is-a-boolean-1-byte-and-not-1-bit-of-size
    bool isdir;                            // dir_entry or file_chunk, CPU can't address anything smaller than a byte
    off_t size;                            // sizeof(dir_entry) or file size
    unsigned magic;                        // Magic number == 4 bytes
  };

struct indirect_inode_disk
  {
    // 1st layer: disk addr of inode_disk
    // 2nd layer: disk addr of indirect_inode_disk
    block_sector_t direct_disk_addrs[127]; // 127 * 4 = 508
    unsigned magic;                         // Magic number == 4 bytes    
  };

struct inode // inode (in memory)
  {
    struct list_elem elem;              /* Element in inode list. */
    block_sector_t sector;              // disk addr of inode_disk
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    struct inode_disk inode_disk;             /* Inode content. */
  };


struct bitmap;

void inode_init (void);
bool inode_create_inode_disk_reserve_disk (block_sector_t, off_t);
struct inode *inode_open_inode_read_inode_disk (block_sector_t);
struct inode *inode_reopen (struct inode *);
block_sector_t inode_get_inumber (const struct inode *);
void inode_close (struct inode *);
void inode_remove (struct inode *);
off_t inode_read_direntry_or_filechunk (struct inode *, void *, off_t size, off_t offset);
off_t inode_write_direntry_or_filechunk (struct inode *, const void *, off_t size, off_t offset);
void inode_deny_write (struct inode *);
void inode_allow_write (struct inode *);
off_t inode_length (const struct inode *);

#endif /* filesys/inode.h */
