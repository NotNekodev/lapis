#include "mm/kheap.h"
#include <util/memory.h>

#include <stdint.h>
#include <stddef.h>

void *memcpy(void *restrict dest, const void *restrict src, size_t n) {
    uint8_t *restrict pdest = dest;
    const uint8_t *restrict psrc = src;

    for (size_t i = 0; i < n; i++) {
        pdest[i] = psrc[i];
    }

    return dest;
}

void *memset(void *s, int c, size_t n) {
    uint8_t *p = s;

    for (size_t i = 0; i < n; i++) {
        p[i] = (uint8_t)c;
    }

    return s;
}

void *memmove(void *dest, const void *src, size_t n) {
    uint8_t *pdest = dest;
    const uint8_t *psrc = src;

    if ((uintptr_t)src > (uintptr_t)dest) {
        for (size_t i = 0; i < n; i++) {
            pdest[i] = psrc[i];
        }
    } else if ((uintptr_t)src < (uintptr_t)dest) {
        for (size_t i = n; i > 0; i--) {
            pdest[i-1] = psrc[i-1];
        }
    }

    return dest;
}

int memcmp(const void *s1, const void *s2, size_t n) {
    const uint8_t *p1 = s1;
    const uint8_t *p2 = s2;

    for (size_t i = 0; i < n; i++) {
        if (p1[i] != p2[i]) {
            return p1[i] < p2[i] ? -1 : 1;
        }
    }

    return 0;
}

int strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }

    return (unsigned char)*s1 - (unsigned char)*s2;
}

int strncmp(const char *s1, const char *s2, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (s1[i] != s2[i]) {
            return (unsigned char)s1[i] - (unsigned char)s2[i];
        }
        if (s1[i] == '\0') {
            return 0;
        }
    }
    return 0;
}

size_t strlen(const char *str) {
    size_t len = 0;
    while (str[len]) {
        len++;
    }
    return len;
}

int strcpy(char *dest, const char *src) {
    size_t i = 0;
    while (src[i]) {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
    return 0;
}

int strncpy(char *dest, const char *src, size_t n) {
    size_t i = 0;
    while (i < n && src[i]) {
        dest[i] = src[i];
        i++;
    }
    if (i < n) {
        dest[i] = '\0';
    }
    return 0;
}

int strcat(char *dest, const char *src) {
    size_t dest_len = strlen(dest);
    size_t i = 0;
    while (src[i]) {
        dest[dest_len + i] = src[i];
        i++;
    }
    dest[dest_len + i] = '\0';
    return 0;
}


char* strdup(char *str) {
    char *ret = kzalloc(strlen(str) + 1);
    if (!ret) {
        return NULL;
    }

    memcpy(ret, str, strlen(str) + 1);

    return ret;
}

char *strchr(const char *str, int c) {
    while (*str) {
        if (*str == (char)c) {
            return (char *)str;
        }
        str++;
    }
    return (c == '\0') ? (char *)str : NULL;
}

char *strrchr(const char *s, int c) {
    const char *last = NULL;
    while (*s) {
        if (*s == (char)c) {
            last = s;
        }
        s++;
    }
    return (char *)((c == '\0') ? s : last);
}

char *strtok(char *str, const char *delim) {
    static char *next = NULL;

    if (str) {
        next = str;
    }
    if (!next) {
        return NULL;
    }

    while (*next && strchr(delim, *next)) {
        next++;
    }

    if (*next == '\0') {
        return NULL;
    }

    char *token_start = next;

    while (*next && !strchr(delim, *next)) {
        next++;
    }

    if (*next) {
        *next = '\0';
        next++;
    } else {
        next = NULL;
    }

    return token_start;
}

char *strtok_r(char *str, const char *delim, char **saveptr) {
    char *next;

    if (str) {
        next = str;
    } else {
        next = *saveptr;
    }

    while (*next && strchr(delim, *next)) {
        next++;
    }

    if (*next == '\0') {
        return NULL;
    }

    char *token_start = next;

    while (*next && !strchr(delim, *next)) {
        next++;
    }

    if (*next) {
        *next = '\0';
        next++;
    } else {
        next = NULL;
    }

    *saveptr = next;
    return token_start;
}