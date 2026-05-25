#include <mm/kheap.h>

#include <mm/page.h>
#include <mm/pmm.h>
#include <mm/pfn_db.h>

#include <util/memory.h>
#include <util/spinlock.h>

#include <log/log.h>

#define KHEAP_ALIGN 16u
#define SLAB_MAGIC 0x534C4142u // "SLAB"
#define BIG_CACHE_INDEX 0xFFFFu

typedef struct slab {
    struct slab *next;
    void *free_list;
    uint32_t magic;
    uint16_t object_size;
    uint16_t capacity;
    uint16_t free_count;
    uint16_t cache_index;
} slab_t;

typedef struct slab_cache {
    uint16_t object_size;
    slab_t *slabs;
    uint32_t slab_count;
} slab_cache_t;

static spinlock_t kheap_lock = SPINLOCK_INIT("kheap");

static const uint16_t cache_sizes[] = {
    16, 32, 64, 128, 256, 512, 1024, 2048
};

#define CACHE_COUNT (sizeof(cache_sizes) / sizeof(cache_sizes[0]))

static slab_cache_t caches[CACHE_COUNT];

static inline uintptr_t align_up_uintptr(uintptr_t val, uintptr_t align) {
    return (val + align - 1) & ~(align - 1);
}

static inline uintptr_t page_base(void *ptr) {
    return (uintptr_t)ALIGN_DOWN((uintptr_t)ptr, PAGE_SIZE);
}

static slab_t *slab_create(uint16_t object_size, uint16_t cache_index) {
    page_t *page = pmm_alloc_page();
    if (!page) {
        return NULL;
    }

    uint64_t phys = pfn_db_page_to_phys(page);
    void *page_ptr = PHYS_TO_VIRT(phys);

    memset(page_ptr, 0, PAGE_SIZE);

    slab_t *slab = (slab_t *)page_ptr;
    slab->magic = SLAB_MAGIC;
    slab->object_size = object_size;
    slab->cache_index = cache_index;

    uintptr_t obj_start = align_up_uintptr((uintptr_t)page_ptr + sizeof(slab_t), KHEAP_ALIGN);
    uintptr_t obj_end = (uintptr_t)page_ptr + PAGE_SIZE;

    if (obj_start >= obj_end) {
        pmm_page_release(page);
        return NULL;
    }

    uint16_t capacity = (uint16_t)((obj_end - obj_start) / object_size);
    if (capacity == 0) {
        pmm_page_release(page);
        return NULL;
    }

    slab->capacity = capacity;
    slab->free_count = capacity;
    slab->free_list = NULL;

    for (uint16_t i = 0; i < capacity; ++i) {
        void *obj = (void *)(obj_start + (uintptr_t)i * object_size);
        *(void **)obj = slab->free_list;
        slab->free_list = obj;
    }

    return slab;
}

static void slab_destroy(slab_t *slab) {
    if (!slab) {
        return;
    }

    uint64_t phys = VIRT_TO_PHYS(slab);
    page_t *page = pfn_db_phys_to_page(phys);
    if (!page) {
        critical("kheap: invalid slab page to free paddr=%#018llx\n", phys);
        return;
    }

    pmm_page_release(page);
}

static slab_cache_t *cache_for_size(size_t size, uint16_t *out_index) {
    for (uint16_t i = 0; i < CACHE_COUNT; ++i) {
        if (size <= caches[i].object_size) {
            if (out_index) {
                *out_index = i;
            }
            return &caches[i];
        }
    }

    return NULL;
}

static void cache_add_slab(slab_cache_t *cache, slab_t *slab) {
    slab->next = cache->slabs;
    cache->slabs = slab;
    cache->slab_count++;
}

static void cache_remove_slab(slab_cache_t *cache, slab_t *slab) {
    slab_t *prev = NULL;
    for (slab_t *cur = cache->slabs; cur; cur = cur->next) {
        if (cur == slab) {
            if (prev) {
                prev->next = cur->next;
            } else {
                cache->slabs = cur->next;
            }
            cur->next = NULL;
            if (cache->slab_count > 0) {
                cache->slab_count--;
            }
            return;
        }
        prev = cur;
    }
}

void kheap_init(void) {
    spinlock_acquire(&kheap_lock);

    for (uint16_t i = 0; i < CACHE_COUNT; ++i) {
        caches[i].object_size = cache_sizes[i];
        caches[i].slabs = NULL;
        caches[i].slab_count = 0;
    }

    spinlock_release(&kheap_lock);

    info("kheap: initialized with %d caches\n", CACHE_COUNT);
}

void *kmalloc(size_t size) {
    if (size == 0) {
        return NULL;
    }

    size = ALIGN_UP(size, KHEAP_ALIGN);

    spinlock_acquire(&kheap_lock);

    uint16_t cache_index = 0;
    slab_cache_t *cache = cache_for_size(size, &cache_index);

    if (!cache) {
        slab_t *slab = slab_create((uint16_t)size, BIG_CACHE_INDEX);
        if (!slab) {
            spinlock_release(&kheap_lock);
            return NULL;
        }
        slab->free_count = 0;
        slab->capacity = 1;
        slab->free_list = NULL;
        slab->next = NULL;

        uintptr_t obj_start = align_up_uintptr((uintptr_t)slab + sizeof(slab_t), KHEAP_ALIGN);
        if (obj_start + size > (uintptr_t)slab + PAGE_SIZE) {
            slab_destroy(slab);
            spinlock_release(&kheap_lock);
            return NULL;
        }

        spinlock_release(&kheap_lock);
        return (void *)obj_start;
    }

    slab_t *slab = cache->slabs;
    if (!slab) {
        slab = slab_create(cache->object_size, cache_index);
        if (!slab) {
            spinlock_release(&kheap_lock);
            return NULL;
        }
        cache_add_slab(cache, slab);
    }

    void *obj = slab->free_list;
    if (!obj) {
        spinlock_release(&kheap_lock);
        return NULL;
    }

    slab->free_list = *(void **)obj;
    if (slab->free_count > 0) {
        slab->free_count--;
    }

    if (slab->free_count == 0) {
        cache_remove_slab(cache, slab);
    }

    spinlock_release(&kheap_lock);
    return obj;
}

void *kzalloc(size_t size) {
    void *ptr = kmalloc(size);
    if (!ptr) {
        return NULL;
    }
    memset(ptr, 0, size);
    return ptr;
}

void kfree(void *ptr) {
    if (!ptr) {
        return;
    }

    slab_t *slab = (slab_t *)page_base(ptr);
    if (slab->magic != SLAB_MAGIC) {
        critical("kheap: invalid free %p (bad slab magic)\n", ptr);
        return;
    }

    spinlock_acquire(&kheap_lock);

    if (slab->cache_index == BIG_CACHE_INDEX || slab->capacity == 1) {
        spinlock_release(&kheap_lock);
        slab_destroy(slab);
        return;
    }

    slab_cache_t *cache = &caches[slab->cache_index];

    if (slab->free_count == 0) {
        cache_add_slab(cache, slab);
    }

    *(void **)ptr = slab->free_list;
    slab->free_list = ptr;

    if (slab->free_count < slab->capacity) {
        slab->free_count++;
    }

    if (slab->free_count == slab->capacity && cache->slab_count > 1) {
        cache_remove_slab(cache, slab);
        spinlock_release(&kheap_lock);
        slab_destroy(slab);
        return;
    }

    spinlock_release(&kheap_lock);
}

void *krealloc(void *ptr, size_t new_size) {
    if (!ptr) {
        return kmalloc(new_size);
    }

    if (new_size == 0) {
        kfree(ptr);
        return NULL;
    }

    slab_t *slab = (slab_t *)page_base(ptr);
    if (slab->magic != SLAB_MAGIC) {
        critical("kheap: invalid realloc %p (bad slab magic)\n", ptr);
        return NULL;
    }

    size_t old_size = slab->object_size;
    if (new_size <= old_size) {
        return ptr;
    }

    void *new_ptr = kmalloc(new_size);
    if (!new_ptr) {
        return NULL;
    }

    memcpy(new_ptr, ptr, old_size);
    kfree(ptr);
    return new_ptr;
}
