#include <mm/pmm.h>

#include <log/log.h>
#include <mm/pfn_db.h>
#include <mm/page.h>
#include <stddef.h>
#include <stdint.h>

static page_t *free_list;
static uint64_t free_pages;
static uint64_t total_pages;

static void free_list_remove(page_t *page) {
    page_t *prev = page->u2.prev;
    page_t *next = page->u1.next;

    if (prev) {
        prev->u1.next = next;
    } else {
        free_list = next;
    }

    if (next) {
        next->u2.prev = prev;
    }

    page->u1.next = NULL;
    page->u2.prev = NULL;
}

static void free_list_push(page_t *page) {
    page->u2.prev = NULL;
    page->u1.next = free_list;
    if (free_list) {
        free_list->u2.prev = page;
    }
    free_list = page;
}

void pmm_init(void) {
    page_t *db = pfn_db_getdb();
    uint64_t max_pfn = pfn_db_getmax();

    free_list = NULL;
    free_pages = 0;
    total_pages = max_pfn + 1;

    if (!db) {
        return;
    }

    for (uint64_t i = 0; i <= max_pfn; ++i) {
        page_t *page = &db[i];

        if (is_page_free(page)) {
            page->u1.next = NULL;
            page->u2.prev = NULL;
            free_list_push(page);
            free_pages++;
        } else {
            page->u1.next = NULL;
            page->u2.prev = NULL;
        }
    }
}

page_t *pmm_alloc_page(void) {
    if (!free_list) {
        return NULL;
    }

    page_t *page = free_list;
    free_list_remove(page);

    if (free_pages > 0) {
        free_pages--;
    }

    page->flags &= ~(PAGE_FREE | PAGE_RESERVED | PAGE_SHARED | PAGE_COW);
    page->flags |= PAGE_ALLOCATED;
    page->refcount = 1;
    page->u2.sharecount = 1;

    return page;
}

void *palloc(void) {
    page_t *page = pmm_alloc_page();
    if (!page) {
        return NULL;
    }
    uint64_t phys = pfn_db_page_to_phys(page);
    return PHYS_TO_VIRT(phys);
}

void pmm_page_retain(page_t *page) {
    if (!page) {
        return;
    }

    if (is_page_free(page)) {
        free_list_remove(page);
        if (free_pages > 0) {
            free_pages--;
        }
        page->flags &= ~(PAGE_FREE | PAGE_RESERVED);
        page->flags |= PAGE_ALLOCATED;
    }

    page->refcount++;
    if (page->refcount == 1) {
        page->u2.sharecount = 1;
    }
}

void pmm_page_release(page_t *page) {
    if (!page || page->refcount == 0) {
        return;
    }

    page->refcount--;
    if (page->refcount > 0) {
        return;
    }

    if (is_page_reserved(page)) {
        return;
    }

    page->flags &= ~(PAGE_ALLOCATED | PAGE_SHARED | PAGE_COW);
    page->flags |= PAGE_FREE;
    page->u2.sharecount = 0;

    if (!is_page_dirty(page)) {
        free_list_push(page);
        free_pages++;
    }
}

void pmm_page_share(page_t *page) {
    if (!page) {
        return;
    }

    if (!is_page_shared(page)) {
        page->flags |= PAGE_SHARED;
        if (page->u2.sharecount == 0) {
            page->u2.sharecount = 1;
        }
    }

    page->u2.sharecount++;
}

void pmm_page_unshare(page_t *page) {
    if (!page) {
        return;
    }

    if (!is_page_shared(page)) {
        return;
    }

    if (page->u2.sharecount > 0) {
        page->u2.sharecount--;
    }

    if (page->u2.sharecount <= 1) {
        page->flags &= ~PAGE_SHARED;
    }
}

void pmm_page_mark_cow(page_t *page) {
    if (!page) {
        return;
    }

    page->flags |= PAGE_COW;
}

void pmm_page_clear_cow(page_t *page) {
    if (!page) {
        return;
    }

    page->flags &= ~PAGE_COW;
}

uint64_t pmm_free_pages(void) {
    return free_pages;
}

uint64_t pmm_total_pages(void) {
    return total_pages;
}

void pmm_dump_stats(void) {
    debug("pmm: FREE pages: %llu / %llu\n", free_pages, total_pages);
    debug("pmm: FREE bytes: %llu\n", free_pages * PAGE_SIZE);
}

void pmm_stress_test(void) {
    uint64_t start_free = pmm_free_pages();
    page_t *list = NULL;
    uint64_t allocated = 0;

    info("pmm: stress start free=%llu total=%llu\n", start_free, pmm_total_pages());

    for (;;) {
        page_t *page = pmm_alloc_page();
        if (!page) {
            break;
        }
        uint64_t phys = pfn_db_page_to_phys(page);
        uint64_t *ptr = (uint64_t *)PHYS_TO_VIRT(phys);
        uint64_t pattern = 0xdeadbeefcafebabeULL ^ allocated;
        ptr[0] = pattern;
        ptr[(PAGE_SIZE / sizeof(uint64_t)) - 1] = ~pattern;
        page->u1.next = list;
        list = page;
        allocated++;
    }

    info("pmm: stress allocated=%llu free=%llu\n", allocated, pmm_free_pages());

    if (allocated != start_free) {
        error("pmm: stress alloc mismatch %llu vs %llu\n", allocated, start_free);
    }

    if (pmm_free_pages() != 0) {
        error("pmm: stress free count not zero %llu\n", pmm_free_pages());
    }

    while (list) {
        page_t *next = list->u1.next;
        uint64_t phys = pfn_db_page_to_phys(list);
        uint64_t *ptr = (uint64_t *)PHYS_TO_VIRT(phys);
        uint64_t index = allocated - 1;
        uint64_t pattern = 0xdeadbeefcafebabe ^ index;
        if (ptr[0] != pattern || ptr[(PAGE_SIZE / sizeof(uint64_t)) - 1] != ~pattern) {
            error("pmm: stress data mismatch pfn=%llu\n", pfn_db_getpfn(list));
        }
        allocated--;
        pmm_page_release(list);
        list = next;
    }

    info("pmm: stress released free=%llu\n", pmm_free_pages());

    if (pmm_free_pages() != start_free) {
        error("pmm: stress release mismatch %llu vs %llu\n", pmm_free_pages(), start_free);
    }
}
