#ifndef _TIME_H
#define _TIME_H 1

#include <stdint.h>

typedef struct timespec {
    int64_t tv_sec;
    int64_t tv_nsec;
} timespec_t;

#endif // _TIME_H