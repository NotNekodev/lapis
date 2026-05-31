#ifndef _PMM_H
#define _PMM_H 1

#include <mm/page.h>

void pmm_init(void);

void *palloc(void);
page_t *pmm_alloc_page(void);
page_t *pmm_alloc_pages(size_t count);

void pmm_page_retain(page_t *page);
void pmm_page_release(page_t *page);
void pmm_pages_release(page_t *page, size_t count);

void pmm_page_share(page_t *page);
void pmm_page_unshare(page_t *page);
void pmm_page_mark_cow(page_t *page);
void pmm_page_clear_cow(page_t *page);

uint64_t pmm_free_pages(void);
uint64_t pmm_total_pages(void);
void pmm_dump_stats(void);
void pmm_stress_test(void);

#endif // _PMM_H