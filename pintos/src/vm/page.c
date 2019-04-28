#include "vm/page.h"
#include "lib/kernel/hash.h"
#include "threads/thread.h"
#include <stdbool.h>

unsigned hash_spt (const struct hash_elem* elem, void* aux);
unsigned hash_spt_less (const struct hash_elem *a, const struct hash_elem *b, void *aux);


/*
 * Initialize supplementary page table
 */
void 
page_init (void)
{
	hash_init(thread_current()->spt, hash_spt, hash_spt_less, NULL);
}

unsigned hash_spt (const struct hash_elem* elem, void* aux)
{
	const void* buf_ = hash_entry(elem, struct sup_page_table_entry, elem)->user_vaddr;
	size_t size = 4;
	return hash_bytes(buf_, size);
}

unsigned hash_spt_less (const struct hash_elem *a, const struct hash_elem *b, void *aux)
{
	return hash_entry(a, struct sup_page_table_entry, elem)->user_vaddr<hash_entry(b, struct sup_page_table_entry, elem)->user_vaddr;
}

/*
 * Make new supplementary page table entry for addr 
 */
struct sup_page_table_entry *
allocate_page (void *addr)
{
	struct sup_page_table_entry* spt_entry;
	spt_entry -> user_vaddr = addr;
	//spt_entry -> access_time = 

	spt_entry->accessed = 0;
	spt_entry->dirty = 0;

	hash_insert(thread_current()->spt, &(spt_entry->elem));
	return spt_entry;
}
