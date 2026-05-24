#ifndef _KHEAP_H
#define _KHEAP_H 1

#include <stddef.h>

void kheap_init(void);
void *kmalloc(size_t size);
void *kzalloc(size_t size);
void kfree(void *ptr);

void *krealloc(void *ptr, size_t new_size);

#endif // _KHEAP_H