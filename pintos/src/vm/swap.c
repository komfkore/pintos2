#include "vm/swap.h"
#include "devices/disk.h"
#include "threads/synch.h"
#include "vm/page.h"
#include "vm/frame.h"
#include "threads/palloc.h"
#include "threads/vaddr.h"
#include "threads/init.h"
#include "filesys/off_t.h"
#include <bitmap.h>
#include <stdbool.h>

/* The swap device */
static struct disk *swap_device;

/* Tracks in-use and free swap slots */
static struct bitmap *swap_table;

/* Protects swap_table */
static struct lock swap_lock;

/* 
 * Initialize swap_device, swap_table, and swap_lock.
 */
void 
swap_init (void)
{
  swap_device = disk_get(1,1);
  if(swap_device == NULL)
  {
	  PANIC("swap disk is null");
  }
  swap_table = bitmap_create(8*1024);
  lock_init(&swap_lock);
}


bool 
swap_in (void *addr) // when page_fault but already evicted addr called.
{
	//lock_acquire(&swap_lock);
	/*
	 * Reclaim a frame from swap device.
	 * 1. Check that the page has been already evicted. 
	 */
	struct sup_page_table_entry *spte = spt_get_page(addr);
	if(!spte->swapped)
	{
		PANIC("Not a swapped page");
	}

	//printf("swap in place: %d\n", spte->swapped_place);
	 /* 2. You will want to evict an already existing frame
	 * to make space to read from the disk to cache. 
	 */
	void *kpage = palloc_get_page (PAL_USER);
    void *upage = (void *)(((uintptr_t)addr >> 12) << 12);
	uint32_t *pd = spte->thread->pagedir;
	//printf("thread current: %s, thread saved: %s\n", thread_current()->name, spte->thread->name);
	if(pagedir_get_page (pd, upage) != NULL)
	{
		PANIC("pagedir get page error");
	}
	if(kpage==NULL)
	{
		//lock_release(&swap_lock);
		bool result = swap_out();
		//lock_acquire(&swap_lock);
		kpage = palloc_get_page (PAL_USER);
		if(kpage==NULL)
		{
			PANIC("getpage error");
		}
	}
	//printf("swapin 1\n");/
	/*
	if(frame_find_addr(&frame_table, kpage)!=NULL)
		PANIC("error");
	//printf("swapin 2\n");
	 /* 3. Re-link the new frame with the corresponding supplementary
	 * page table entry. 
	 */
	spte->swapped = false;
	allocate_frame(kpage, upage);
	//printf("swapin 3\n");
	if(!pagedir_set_page(pd, upage, kpage, true))
	{
		//printf("swap mapping failed\n");
	}
	
	//printf("swapin 4\n");


	 /* 4. Do NOT create a new supplementray page table entry. Use the 
	 * already existing one. 
	 */


	 /* 5. Use helper function read_from_disk in order to read the contents
	 * of the disk into the frame. 
	 */ 
	//printf("swap in uaddr: %x\n", upage);
	int i = 0;
	//lock_acquire(&swap_lock);
	for(i=0; i<8; i++)
	{
		read_from_disk(kpage, spte->swapped_place, i);
	}
	bitmap_set_multiple(swap_table, spte->swapped_place, 8, false);
	//lock_release(&swap_lock);
	if(pagedir_get_page (pd, upage) == NULL)
	{
		PANIC("pagedir get page is null after swap in");
	}
	return true;
}


bool
swap_out (void) // when palloc is null, page full.
{
	/* 
	 * Evict a frame to swap device. 
	 * 1. Choose the frame you want to evict. 
	 * (Ex. Least Recently Used policy -> Compare the timestamps when each 
	 * frame is last accessed)
	 */
	//lock_acquire(&swap_lock);
	struct frame_table_entry* fte = find_frame_to_evict(); // pick not evicted one?
	if(fte==NULL)
	{
		PANIC("fte of swap out is null");
	}
	void* upage = fte->upage;
	void* kpage = fte->kpage;
	struct sup_page_table_entry * spte = spt_get_page(upage);
	 /* 2. Evict the frame. Unlink the frame from the supplementray page table entry
	 * Remove the frame from the frame table after freeing the frame with
	 * pagedir_clear_page. 
	 */

	uint32_t *pd = thread_current()->pagedir;
	pagedir_clear_page(pd, upage);
	//fte -> upage = NULL;

	 /* 3. Do NOT delete the supplementary page table entry. The process
	 * should have the illusion that they still have the page allocated to
	 * them. 
	 */

	 /* 4. Find a free block to write you data. Use swap table to get track
	 * of in-use and free swap slots.
	 */
	bool dirty_bit = fte->dirty;
	dirty_bit = dirty_bit||pagedir_is_dirty(pd, upage)||pagedir_is_dirty(pd, kpage);
	if(!dirty_bit)
	{
		//printf("addr3: %x\n", upage);
		PANIC("not dirty");
		free_frame(kpage);
		pagedir_set_dirty(pd, upage, false);
		pagedir_set_dirty(pd, kpage, false);
		palloc_free_page(kpage);
		//lock_release(&swap_lock);
		return true;
	}
	size_t place = bitmap_scan(swap_table, 0, 8, false);
	//printf("place: %u\n", place);
	if(place==BITMAP_ERROR)
	{
		PANIC("swap slots are fully used.");
	}
	//printf("swap out uaddr: %x\n", upage);
	int i=0;
	//lock_acquire(&swap_lock);
	for(i=0; i<8; i++)
	{
		//printf("addr4 upage: %x\n", upage);
		//printf("addr4 kpage: %x\n", kpage);
		write_to_disk(kpage, place, i);
	}
	//안됨
	//printf("reached1\n");
	bitmap_set_multiple(swap_table, place, 8, true);
	//lock_release(&swap_lock);
	//list_remove(&fte->elem); // problem
	pagedir_set_dirty(pd, upage, false);
	pagedir_set_dirty(pd, kpage, false);
	//printf("reached2\n");
	free_frame(kpage);
	//printf("reached3\n");
	palloc_free_page(kpage); // add
	//printf("reached4\n");
	//free_frame(kpage);
	spte -> swapped = true;
	spte->swapped_place = place;
	//printf("swap out place: %d\n", place);
	

	//lock_release(&swap_lock);
	return true;


}

/* 
 * Read data from swap device to frame. 
 * Look at device/disk.c
 */
void read_from_disk (uint8_t *frame, size_t place, int index)
{

	disk_read(swap_device, place+index, frame+index*512);

}

/* Write data to swap device from frame */
void write_to_disk (uint8_t *frame, size_t place, int index)
{
	/*struct list_elem *frame_elem = frame_find_addr (&frame_table, frame); // not work?
	struct frame_table_entry *fte = list_entry(frame_elem, struct frame_table_entry, elem);
	struct sup_page_table_entry *spte = spt_get_page(fte->upage);
	if(spte->swapped)
	{
		printf("swapin addr: %x\n", fte->upage);
		swap_in(fte->upage);
	}*/
	disk_write(swap_device, place+index, frame+index*512); // evicted page called -> error?

}

