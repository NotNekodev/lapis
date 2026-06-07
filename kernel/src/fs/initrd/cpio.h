// cpio newc archive parser
#ifndef _CPIO_H
#define _CPIO_H 1

#include <stdint.h>
#include <stddef.h>

typedef struct cpio_entry {
    char cmagic[6];
    uint64_t ino;
    uint64_t mode;
    uint64_t uid;
    uint64_t gid;
    uint64_t nlink;
    uint64_t mtime;
    uint64_t filesize;
    uint64_t devmajor;
    uint64_t devminor;
    uint64_t rdevmajor;
    uint64_t rdevminor;
    uint64_t namesize;
    uint64_t check; // unused

    char *name;
    void *data;
} cpio_entry_t;

typedef struct cpio_archive {
    cpio_entry_t *entries;
    size_t entry_count;
    void* archive_data;
    size_t archive_size;
} cpio_archive_t;

int cpio_archive_parse(cpio_archive_t *archive, void *data, size_t size);
int cpio_archive_extract(cpio_archive_t *archive, char *dest_path);

void cpio_archive_free(cpio_archive_t *archive);

#endif // _CPIO_H