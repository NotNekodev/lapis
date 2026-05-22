#include "arch/io.h"
#include "log/log.h"
#include "mm/page.h"
#include "stddef.h"
#include <mm/pfn_db.h>

static page_t *mem_map;
static uint64_t max_pfn;
static uint64_t pfn_db_phys;

static void _pfn_db_calc_max_pfn(struct limine_memmap_response *mmap) {
	max_pfn = 0;

	for (uint64_t i = 0; i < mmap->entry_count; i++) {
		struct limine_memmap_entry *e = mmap->entries[i];

		if (e->type != LIMINE_MEMMAP_USABLE)
			continue;

		uint64_t end = (e->base + e->length + PAGE_SIZE - 1) / PAGE_SIZE;

		if (end > max_pfn)
			max_pfn = end;
	}
}

static void _pfn_db_reserve(struct limine_memmap_response *mmap) {
	uint64_t size = (max_pfn + 1) * sizeof(page_t);
	size = (size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

	for (uint64_t i = 0; i < mmap->entry_count; i++) {
		struct limine_memmap_entry *e = mmap->entries[i];

		if (e->type != LIMINE_MEMMAP_USABLE)
			continue;

		if (e->length < size)
			continue;

		pfn_db_phys = e->base;

		e->base += size;
		e->length -= size;

		return;
	}

	critical("pfndb: no usable region large enough, need %llu bytes", size);
    hcf();
}

void pfn_db_dump(void) {
	uint64_t range_start = 0;
	uint8_t cur = mem_map[0].flags;
	uint64_t free_pages = 0;

	if (cur & PAGE_FREE)
		free_pages++;

	for (uint64_t i = 1; i <= max_pfn; i++) {
		uint8_t f = mem_map[i].flags;

		if (f & PAGE_FREE)
			free_pages++;

		if (f != cur) {
			const char *state = (cur & PAGE_ALLOCATED) ? "ALLOCATED" :
								(cur & PAGE_RESERVED)  ? "RESERVED" :
								(cur & PAGE_FREE)	   ? "FREE" :
													     "UNKNOWN";

			debug("pfndb: [0x%016llx - 0x%016llx] %s\n",
					 range_start * PAGE_SIZE, (i - 1) * PAGE_SIZE, state);

			range_start = i;
			cur = f;
		}
	}

	const char *state = (cur & PAGE_ALLOCATED) ? "ALLOCATED" :
						(cur & PAGE_RESERVED)  ? "RESERVED" :
						(cur & PAGE_FREE)	   ? "FREE" :
												 "UNKNOWN";

	debug("pfndb: [0x%016llx - 0x%016llx] %s\n", range_start * PAGE_SIZE,
			 max_pfn * PAGE_SIZE, state);

	debug("pfndb: FREE pages: %llu / %llu\n", free_pages, max_pfn + 1);
	debug("pfndb: FREE bytes: %llu\n", free_pages * PAGE_SIZE);
}

void pfn_db_init(struct limine_memmap_response *memmap) {
	_pfn_db_calc_max_pfn(memmap);
	_pfn_db_reserve(memmap);

	mem_map = (page_t *)PHYS_TO_VIRT(pfn_db_phys);
	if (!mem_map) {
        critical("pfndb: failed to map PFNDB");
        hcf();
    }

	for (uint64_t i = 0; i <= max_pfn; i++) {
		mem_map[i].flags = PAGE_RESERVED;
		mem_map[i].refcount = 0;

		if (i < max_pfn)
			mem_map[i].u1.next = &mem_map[i + 1];
		else
			mem_map[i].u1.next = NULL;
	}

	for (uint64_t i = 0; i < memmap->entry_count; i++) {
		struct limine_memmap_entry *e = memmap->entries[i];

		if (e->type != LIMINE_MEMMAP_USABLE)
			continue;

		uint64_t start = e->base / PAGE_SIZE;
		uint64_t end = (e->base + e->length) / PAGE_SIZE;

		for (uint64_t pfn = start; pfn < end && pfn <= max_pfn; pfn++) {
			mem_map[pfn].flags =
				(mem_map[pfn].flags & ~(PAGE_RESERVED | PAGE_ALLOCATED)) | PAGE_FREE;
		}
	}

	// this is here because UEFI does some weird stuff
	mem_map[0].flags = PAGE_RESERVED;
	mem_map[0].refcount = 0;
}

page_t *pfn_db_getdb(void) {
	return mem_map;
}

uint64_t pfn_db_getmax(void) {
	return max_pfn;
}

uint64_t pfn_db_pfnaddr(uint64_t pfn)
{
	if (pfn > max_pfn)
		return 0;
	return pfn * PAGE_SIZE;
}

page_t *pfn_db_getptr(uint64_t pfn)
{
	if (pfn > max_pfn)
		return NULL;

	return &mem_map[pfn];
}

uint64_t pfn_db_getpfn(page_t *page)
{
	if (!page || !mem_map)
		return (uint64_t)-1;

	if (page < mem_map || page > &mem_map[max_pfn])
		return (uint64_t)-1;

	return (uint64_t)(page - mem_map);
}

uint64_t pfn_db_page_to_phys(page_t *page)
{
	if (!page || !mem_map)
		return 0;

	if (page < mem_map || page > &mem_map[max_pfn])
		return 0;

	return (uint64_t)(page - mem_map) * PAGE_SIZE;
}

page_t *pfn_db_phys_to_page(uint64_t phys)
{
	uint64_t pfn = phys / PAGE_SIZE;

	if (pfn > max_pfn)
		return NULL;

	return &mem_map[pfn];
}
