#ifndef _MEMORY_H
#define _MEMORY_H 1

#include <stddef.h>
#include <stdint.h>

void *memcpy(void *restrict dest, const void *restrict src, size_t n);
void *memset(void *s, int c, size_t n);
void *memmove(void *dest, const void *src, size_t n);
int memcmp(const void *s1, const void *s2, size_t n);

int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, size_t n);
size_t strlen(const char *str);   
char* strdup(char *str);
int strcpy(char *dest, const char *src);
int strncpy(char *dest, const char *src, size_t n);
int strcat(char *dest, const char *src);

char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);
char *strtok(char *str, const char *delim);
char *strtok_r(char *str, const char *delim, char **saveptr);
uint64_t strtoull(const char *str, const char **endptr, int base);

#endif // _MEMORY_H
