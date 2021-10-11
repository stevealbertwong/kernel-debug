/**
 * class scope:
 * - init(), u() inode
 * - r(), w() inode_disk
 * - direct, indirect inode_disk
 * - non-fragmentation
 * 
 */ 
#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include <stdio.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"

#define DIRECT_INODE_DISK_SIZE 123
#define INDIRECT_INODE_DISK_SIZE 127

static struct list open_inodes;

static block_sector_t virtual_disk_addr_to_real_disk_sector (const struct inode *inode, off_t pos); 
static inline size_t size_to_sectors (off_t size);
/***************************************************************/
// key helpers












/***************************************************************/
/**
 * given subdir inode_disk + pos, look for a dir_entry of file/subdir 
 * given file inode_disk + pos, look for 317kb byte of a file
 * return DISK_SECTOR virtual_disk_addr is in
 * 
 * TODO: extend pos into direct, indirect block large
 * virtual_disk_addr: e.g. virtual_disk_addr += struct dir_entry // for loop every file in dir
 */ 
static block_sector_t
virtual_disk_addr_to_real_disk_sector (const struct inode *inode, off_t virtual_disk_addr) 
{
  // 0. error checking
  ASSERT (inode != NULL);
  if (virtual_disk_addr >= inode->inode_disk.size) { // size == bytes
    return -1;
  }
  off_t real_disk_sector = virtual_disk_addr / BLOCK_SECTOR_SIZE;
  ASSERT (real_disk_sector < DIRECT_INODE_DISK_SIZE + INDIRECT_INODE_DISK_SIZE + INDIRECT_INODE_DISK_SIZE*INDIRECT_INODE_DISK_SIZE);

  // 1. direct block - e.g. 121 disk sector
  if(real_disk_sector <= DIRECT_INODE_DISK_SIZE){ 
    return inode->inode_disk.direct_disk_addrs[real_disk_sector];
  
  // 2. 1st indirect block - e.g. 123 + 2 = 125 disk sector
  } else if (real_disk_sector < DIRECT_INODE_DISK_SIZE + INDIRECT_INODE_DISK_SIZE){     
    struct inode *first_layer_inode = inode_open_inode_read_inode_disk(inode->inode_disk.one_layer_inode_disk);    
    real_disk_sector = real_disk_sector - DIRECT_INODE_DISK_SIZE; // 2
    free(first_layer_inode);
    return first_layer_inode->inode_disk.direct_disk_addrs[real_disk_sector];

  // 3. 2nd indirect block
  } else { // e.g. 123 + 127 + (127*3+7) = 511 disk sector 
    
    struct inode *second_layer_inode = inode_open_inode_read_inode_disk(inode->inode_disk.two_layer_inode_disk);
    
    // 3.1 -> 4 in second layer
    off_t second_layer_disk_sector = (real_disk_sector - DIRECT_INODE_DISK_SIZE - INDIRECT_INODE_DISK_SIZE) / INDIRECT_INODE_DISK_SIZE;

    struct inode *first_layer_inode = inode_open_inode_read_inode_disk(second_layer_inode->inode_disk.direct_disk_addrs[second_layer_disk_sector]);
    
    // 7 in first layer
    real_disk_sector = (real_disk_sector - DIRECT_INODE_DISK_SIZE - INDIRECT_INODE_DISK_SIZE) % INDIRECT_INODE_DISK_SIZE; 
    free(second_layer_inode);
    free(first_layer_inode);
    return first_layer_inode->inode_disk.direct_disk_addrs[real_disk_sector];

  }
}

/***************************************************************/
// key APIs 






/***************************************************************/
/**
 * read() inode_disk->dir_entry/file_chunks from Disk into RAM
 * from offset, size/PAGE_SIZE times, into buffer
 * 
 * inode == directory_you_are_in->inode / file->inode
 * buffer == dir_entry/file_chunks
 * 
 * ofs += sizeof e
 */ 
off_t
inode_read_direntry_or_filechunk (struct inode *inode, void *buffer_, off_t size, off_t disk_addr) 
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  uint8_t *bounce = NULL;

  while (size > 0) 
    {
      // 1. given dir pos/file byte, read() diskaddr of dir_entries
      block_sector_t diskaddr_direntries_filechunks = virtual_disk_addr_to_real_disk_sector (inode, disk_addr);
      
      // 2. for loop dir_entries/filechunks within 1 disk block
      int sector_ofs = disk_addr % BLOCK_SECTOR_SIZE;
      // bytes left in inode_disk->size, bytes left in sector -> lesser of the 2
      off_t inode_left = inode_length (inode) - disk_addr;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      // chunk_size == num of bytes actually written into this sector
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      // full size, read() entire 512KB
      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          // read() 1 dir_entry / file_chunk into buffer
          block_read (fs_device, diskaddr_direntries_filechunks, buffer + bytes_read);
        }
      else // not full size, read() 512KB into temp, then memcpy()
        {
          if (bounce == NULL) 
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }
          block_read (fs_device, diskaddr_direntries_filechunks, bounce);
          memcpy (buffer + bytes_read, bounce + sector_ofs, chunk_size);
        }
      // advance
      size -= chunk_size;
      disk_addr += chunk_size;
      bytes_read += chunk_size;
    }
  free (bounce);

  return bytes_read;
}

/**
 * write data into disk indexed() by inode_disk
 * 
 * inode_disk ALREADY EXISTED
 * e.g. filesys_create() == inode_create_inode_disk_reserve_disk() + inode_write_direntry_or_filechunk()
 * 
 */ 
off_t
inode_write_direntry_or_filechunk (struct inode *inode, const void *buffer_, off_t size,
                off_t offset) 
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
  uint8_t *bounce = NULL;

  if (inode->deny_write_cnt){
    // printf("inode.c inode->deny_write_cnt \n");
    return 0;
  }

  // if beyond EOF, file growth by reserving more 0s 
  if(offset + size > inode->inode_disk.size){
    off_t growth_size = offset + size - inode->inode_disk.size;
    inode_reserve_disk(inode->inode_disk, inode->inode_disk.size, size + offset); // size + 1 ??
  }

  while (size > 0) 
    {
      block_sector_t diskaddr_direntries_filechunks = virtual_disk_addr_to_real_disk_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          cache_block_write (fs_device, diskaddr_direntries_filechunks, buffer + bytes_written);
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
            block_read (fs_device, diskaddr_direntries_filechunks, bounce);
          else
            memset (bounce, 0, BLOCK_SECTOR_SIZE);
          memcpy (bounce + sector_ofs, buffer + bytes_written, chunk_size);
          cache_block_write (fs_device, diskaddr_direntries_filechunks, bounce);
        }

      // advance
      // printf("inode.c inode_write_direntry_or_filechunk() chunk_size %d \n", chunk_size);
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }
  free (bounce);

  return bytes_written;
}


/**
 * create inode_disk(dir_entry/file_chunks)in disk 
 * + reserve disk space for dir_entry/file_chunk in non-fragmented way
 * + index() disk addr in new_inode_disk{} tree
 * 
 * called when 1st creating dir_entry 
 * 
 * 1. reserve() inode_disk addr
 * 2. calloc() + populate() inode_disk
 * 3. w() 0s to reserve space for dir_entry/file_chunks
 * 4. w() inode_disk (dir_entry/file_chunks)
 */
bool
inode_create_inode_disk_reserve_disk (block_sector_t reserved_new_inode_disk_addr, off_t size, bool isdir)
{
  struct inode_disk *new_inode_disk = NULL;
  bool success = false;
  ASSERT (size >= 0);
  ASSERT (sizeof *new_inode_disk == BLOCK_SECTOR_SIZE); // inode == 1 sector in size
  // TODO: check size < 2TB ??

  // 1. calloc() new dir_entry/file_chunk's inode_disk
  new_inode_disk = calloc (1, sizeof *new_inode_disk);
  if (!new_inode_disk){
    PANIC("inode_create_inode_disk_reserve_disk() calloc() failed \n");
  }
  new_inode_disk->size = size; // in bytes
  new_inode_disk->isdir = isdir;
  new_inode_disk->magic = INODE_MAGIC;

  inode_reserve_disk(new_inode_disk, 0, size);


DONE: 
  cache_block_write (fs_device, reserved_new_inode_disk_addr, new_inode_disk);//w() inode_disk (dir_entry/file_chunks)
  free (new_inode_disk);
  return true;
}


/**
 * called by file growth when write() 
 * + reserve space when create dir_entry
 * 
 * 
 */ 
bool
inode_reserve_disk (struct inode_disk *inode_disk, off_t start, off_t size){
  
  static char zeros[BLOCK_SECTOR_SIZE]; // entire dir_entries / file_chunks disk sector is 0s

  // EMPTY subdir/file !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! 
  if(size == 0) return 1;

  int num_sectors = size_to_sectors (size); //in 512 bytes
  ASSERT (num_sectors > 0);
  if(num_sectors > DIRECT_INODE_DISK_SIZE + INDIRECT_INODE_DISK_SIZE + INDIRECT_INODE_DISK_SIZE*INDIRECT_INODE_DISK_SIZE) {
    PANIC("inode_reserve_disk() trying to reserve file space bigger than inode_disk could index() \n");
  }

  // 2. w() 0s to new_inode_disk{} tree !!!!!!!!
  // DIRECT BLOCK !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! 
  for(size_t i = 0; i < num_sectors; i++){ // file > 63kb
    if (!free_map_allocate (1, &inode_disk->direct_disk_addrs[i])){
      PANIC("inode_create_inode_disk_reserve_disk() free_map_allocate() direct_disk_addr failed");
    }    
    cache_block_write (fs_device, &inode_disk->direct_disk_addrs[i], zeros);
  }
  num_sectors = num_sectors - DIRECT_INODE_DISK_SIZE;
  if(num_sectors <= 0) return 1; // DONE


  // 1st INDIRECT BLOCK !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!   
  free_map_allocate (1, &inode_disk->one_layer_inode_disk);
  struct indirect_inode_disk *first_layer_inode_disk = calloc (1, sizeof(struct indirect_inode_disk)); 

  for(size_t i = 0; i < num_sectors; i++){ // file > 128kb
    if (!free_map_allocate (1, &first_layer_inode_disk->direct_disk_addrs[i])){
      PANIC("inode_create_inode_disk_reserve_disk() free_map_allocate() direct_disk_addr failed");
    }
    cache_block_write (fs_device, &first_layer_inode_disk->direct_disk_addrs[i], zeros);
  }

  cache_block_write (fs_device, inode_disk->one_layer_inode_disk, first_layer_inode_disk);
  free(first_layer_inode_disk);
  
  num_sectors = num_sectors - INDIRECT_INODE_DISK_SIZE;
  if(num_sectors <= 0) return 1; // DONE


  // 2nd INDIRECT BLOCK !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!   
  free_map_allocate (1, &inode_disk->two_layer_inode_disk);
  struct indirect_inode_disk *second_layer_inode_disk = calloc (1, sizeof(struct indirect_inode_disk)); 

  for(size_t i = 0; i < num_sectors - 128 + 123 + 130; i++) { // file > 8MB
    struct indirect_inode_disk *first_layer_inode_disk = calloc (1, sizeof(struct indirect_inode_disk)); 
    
    for(size_t i = 0; i < 130; i++){
      if (!free_map_allocate (1, &first_layer_inode_disk->direct_disk_addrs[i])){
        PANIC("inode_create_inode_disk_reserve_disk() free_map_allocate() direct_disk_addr failed");
      }
      cache_block_write (fs_device, first_layer_inode_disk->direct_disk_addrs[i], zeros);
    }
    
    if (!free_map_allocate (1, &second_layer_inode_disk->direct_disk_addrs[i])){
      PANIC("inode_create_inode_disk_reserve_disk() free_map_allocate() direct_disk_addr failed");
    }
    cache_block_write (fs_device,  &second_layer_inode_disk->direct_disk_addrs[i], first_layer_inode_disk);

  }
  cache_block_write (fs_device, inode_disk->two_layer_inode_disk, second_layer_inode_disk);
  free(second_layer_inode_disk);

  return true;

}



/**
 * open inode w populated inode_disk
 * 
 * 1. check if inode cache() in open list (i.e. inode_cache) first
 * 2. if not inode_cache, check page_cache
 * 3. if not, read() inode_disk from disk
 * 
 * sector: disk address of inode_disk
 */ 
struct inode *
inode_open_inode_read_inode_disk (block_sector_t sector)
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

  // 2. TODO: if not inode_cache, check page_cache 



  // 3. if not, read() inode_disk from disk 
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;

  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;

  cache_block_read (fs_device, inode->sector, &inode->inode_disk);
  return inode;
}


/**
 * 
 * 
 * 
 * TODO
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

          free_map_release (inode->inode_disk.start,
                            size_to_sectors (inode->inode_disk.length)); 
        }
      free (inode); 
    }
}





/***************************************************************/
// helpers
















/***************************************************************/


static inline size_t
size_to_sectors (off_t size)
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