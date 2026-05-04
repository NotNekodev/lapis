#ifndef _KERNEL_H
#define _KERNEL_H 1

#include "limine.h"

typedef struct kernel_info {
    struct limine_framebuffer *framebuffer;
} kernel_info_t;

extern kernel_info_t kernel_info;

#endif // _KERNEL_H