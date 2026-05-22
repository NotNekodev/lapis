#ifndef _PFN_DB_H
#define _PFN_DB_H 1

#include <mm/page.h>

void pfn_db_init(struct limine_memmap_response *memmap);

page_t *pfn_db_getdb(void);
uint64_t pfn_db_getmax(void);

uint64_t pfn_db_pfnaddr(uint64_t pfn);
page_t *pfn_db_getptr(uint64_t pfn);
uint64_t pfn_db_getpfn(page_t *page);
page_t *pfn_db_phys_to_page(uint64_t phys);
uint64_t pfn_db_page_to_phys(page_t *page);

void pfn_db_dump(void);

#endif // _PFN_DB_H