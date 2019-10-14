#include <bitmap.h>
#include <stdio.h>
#include "threads/vaddr.h"
#include "devices/block.h"
#include "vm/swap.h"
#include "threads/synch.h"

/**
 * swap bitmap
 * - data structure in RAM to index() free "swap" disk sectors
 * - 1 global swap
 * - 1 bit == 1 free disk page 4096KB, 8 disk sectors of 512KB
 */ 
static struct bitmap *free_swap_disk_pages;
static struct block *swap_disk_partition; // device driver object in kernel pool
static const size_t SECTORS_PER_PAGE = PGSIZE/BLOCK_SECTOR_SIZE;//8:4096/512
static struct lock swap_lock;

/**
 * called in init.c main()
 * 
 * 1. init() disk partition object in kernel pool
 * 2. init() bitmap
 * 
 * command pintos-mkdisk swap.dsk --swap-size=n 
 * - creates an disk named `swap.dsk' that contains a n-MB swap partition.
 * - use the BLOCK_SWAP block device for swapping 
 * - block_get_role(): obtain struct block that represents BLOCK_SWAP block device
 */ 
void vm_swap_init(void){
    // 1. device driver API: init() disk partition object in kernel pool
    swap_disk_partition = block_get_role(BLOCK_SWAP);
    if(swap_disk_partition == NULL) {
        PANIC ("vm_swap_init() failed to get disk partition object \n");
    }
    // 2. init() bitmap according to disk partition object 
    size_t swap_total_pages = block_size(swap_disk_partition) / SECTORS_PER_PAGE;
    printf("swap_total_pages %d \n", swap_total_pages);
    free_swap_disk_pages = bitmap_create(swap_total_pages);
    if(!free_swap_disk_pages){
        PANIC("vm_swap_init() failed \n");
    }
    bitmap_set_all(free_swap_disk_pages, true);
    lock_init (&swap_lock);  
}


/**
 * 1. search() bitamp for free 8 disk sectors 
 * 2. block_write() 1 kpage to swap disk
 * 3. u() bitamp
 * 
 * return index of bitmap written
 * 
 * NOTE: upage/kpage ?? depends if block_write() supports page_fault() ??
 */
uint32_t vm_swap_flush_kpage_to_disk(void* kpage){
    
    lock_acquire (&swap_lock);
    printf("vm_swap_flush_kpage_to_disk() called \n");
    // 1. search() bitamp for free 1 disk sectors 
    size_t free_swap_disk_sector = bitmap_scan (free_swap_disk_pages, 
        /*start*/0, /*cnt*/1, true); // 1 bit == 1 disk page == 8 disk sectors
    if(!free_swap_disk_sector){
        PANIC("vm_swap_flush_kpage_to_disk() failed, no more swap disk space left \n");
    }
    // 2. block_write() 1 kpage to swap disk
    size_t i;
    for (i = 0; i < SECTORS_PER_PAGE; ++ i) {//1 kpage == 8 disk sectors
        block_write (swap_disk_partition, // device driver object
            free_swap_disk_sector * SECTORS_PER_PAGE + i, // disk sector number of 1 page
            kpage + (BLOCK_SECTOR_SIZE * i) // src: buffer/kpage 
        );
    }
    
    // 3. u() bitamp
    bitmap_set(free_swap_disk_pages, free_swap_disk_sector, true);
    lock_release (&swap_lock);

    return free_swap_disk_sector;
}


/**
 * 1. block_read() 1 page/8 disk sectors from swap disk to kpage
 * 2. u() bitamp for free swap disk sector
 */
bool vm_swap_read_kpage_from_disk(uint32_t bitmap_index, void *kpage){
    // TODO: checking for errors

    // 1. block_read() 1 page/8 disk sectors from swap disk to kpage
    // why not just read() entire page 1 time, why read sector by sector ??
    size_t i;
    for (i = 0; i < SECTORS_PER_PAGE; ++ i) {//1 kpage == 8 disk sectors
        block_read (swap_disk_partition, // device driver object
            bitmap_index * SECTORS_PER_PAGE + i, // disk sector number of 1 page
            kpage + (BLOCK_SECTOR_SIZE * i) // dest: buffer/kpage
        );
    }

    // 2. u() bitamp for free swap disk sector
    bitmap_set(free_swap_disk_pages, bitmap_index, true);
    
    return true;
}

void 
vm_swap_free(uint32_t bitmap_index){
    bitmap_set(free_swap_disk_pages, bitmap_index, true);

}

