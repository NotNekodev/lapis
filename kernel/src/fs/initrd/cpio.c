#include "log/log.h"
#include "mm/kheap.h"
#include "util/errno.h"
#include <fs/initrd/cpio.h>
#include <fs/vfs/vfs.h>
#include <fs/vfs/kfile.h>

#include <util/memory.h>

#define align4(x) (((x) + 3) & ~3)

typedef struct cpio_reader {
    uint8_t *pos;
    uint8_t *end;
} cpio_reader_t;

static uint64_t parse_hex(const char *buf) {
    char temp[9] = {0};
    memcpy(temp, buf, 8);
    temp[8] = '\0';
    return strtoull(temp, NULL, 16);
}

static int cpio_reader_next(cpio_reader_t *reader, cpio_entry_t *entry) {
    if ((size_t)(reader->end - reader->pos) < 110) {
        warn("cpio: Not enough data for header\n");
        return -1;
    }

    if (memcmp(reader->pos, "070701", 6) != 0 && memcmp(reader->pos, "070702", 6) != 0) {
        warn("cpio: Invalid magic\n");
        return -1;
    }

    uint8_t *pos = reader->pos + 6;

    entry->ino = parse_hex((char *)pos);
    pos += 8;
    entry->mode = parse_hex((char *)pos);
    pos += 8;
    entry->uid = parse_hex((char *)pos);
    pos += 8;
    entry->gid = parse_hex((char *)pos);
    pos += 8;
    entry->nlink = parse_hex((char *)pos);
    pos += 8;
    entry->mtime = parse_hex((char *)pos);
    pos += 8;
    entry->filesize = parse_hex((char *)pos);
    pos += 8;
    entry->devmajor = parse_hex((char *)pos);
    pos += 8;
    entry->devminor = parse_hex((char *)pos);
    pos += 8;
    entry->rdevmajor = parse_hex((char *)pos);
    pos += 8;
    entry->rdevminor = parse_hex((char *)pos);
    pos += 8;
    entry->namesize = parse_hex((char *)pos);
    pos += 8;
    entry->check = parse_hex((char *)pos);
    pos += 8;

    reader->pos = pos;

    if ((size_t)(reader->end - reader->pos) < entry->namesize) {
        warn("cpio: Not enough data for filename\n");
        return -1;
    }

    char *filename = (char *)reader->pos;
    reader->pos += entry->namesize;
    reader->pos  = (uint8_t *)align4((uintptr_t)reader->pos);

    if (filename[entry->namesize - 1] != '\0') {
        warn("cpio: Filename not null-terminated\n");
        return -1;
    }

    if (strcmp(filename, "TRAILER!!!") == 0) {
        return 1;
    }

    entry->name = filename;

    if ((size_t)(reader->end - reader->pos) < entry->filesize) {
        warn("cpio: Not enough data for file content\n");
        return -1;
    }

    entry->data = reader->pos;
    reader->pos += entry->filesize;
    reader->pos  = (uint8_t *)align4((uintptr_t)reader->pos);

    return 0;
}

int cpio_archive_parse(cpio_archive_t *archive, void *data, size_t size) {
    cpio_reader_t reader = {
        .pos = (uint8_t *)data,
        .end = (uint8_t *)data + size,
    };

    if (!archive || !data || size == 0) {
        return -1;
    }

    archive->entries = NULL;
    archive->entry_count = 0;
    archive->archive_data = data;
    archive->archive_size = size;

    size_t capacity = 4;
    archive->entries = kzalloc(capacity * sizeof(cpio_entry_t));
    if (!archive->entries)
        return -1;

    while (reader.pos < reader.end) {
        if (archive->entry_count == capacity) {
            capacity *= 2;
            cpio_entry_t *new_entries = krealloc(archive->entries, capacity * sizeof(cpio_entry_t));
            if (!new_entries)
                return -1;
            archive->entries = new_entries;
        }

        cpio_entry_t *entry = &archive->entries[archive->entry_count];
        memset(entry, 0, sizeof(cpio_entry_t));
        int res = cpio_reader_next(&reader, entry);
        if (res == 1)
            break; // End marker
        if (res < 0)
            return -1;

        archive->entry_count++;
    }

    return 0;
}

static const char *normalize_dest(const char *dest) {
    if (strcmp(dest, "/") == 0)
        return "";   // root mount: no prefix
    return dest;
}

void cpio_archive_free(cpio_archive_t *archive) {
    if (archive->entries) {
        kfree(archive->entries);
        archive->entries = NULL;
    }
    archive->entry_count = 0;
    archive->archive_data = NULL;
    archive->archive_size = 0;
}


int cpio_archive_extract(cpio_archive_t *archive, char *dest_path) {
    if (!archive || !dest_path) {
        warn("Missing CPIO archive, or destination path (%p, %p).\n", archive, dest_path);
        return -EFAULT;
    }

    const char *base = normalize_dest(dest_path);

    // track the full path of a file
    size_t s = 20;
    char *path = kmalloc(s);
    memset(path, 0, s);
    if ((strlen(dest_path) + 1) > s) {
        path = krealloc(path, (strlen(dest_path) + 1));
        s = (strlen(dest_path) + 1);
    }

    info("Extracting CPIO to %s\n", dest_path);

    // Create the directory first
    if (strcmp(dest_path, "/") != 0) {
        vfs_mkdir(dest_path, 0755);
    }


    for (size_t i = 0; i < archive->entry_count; i++) {
        memset(path, 0, s);
        strcat(path, base);

        cpio_entry_t *file = &archive->entries[i];

        char *fname = file->name;
        if (fname[0] == '/')
            fname++;

        char *name_dup = strdup(fname);

        if (file->namesize + 1 > s) {
            s = file->namesize + 1;
            path = krealloc(path, s);
        }


        char *save;
        char *dir = strtok_r(name_dup, "/", &save);

        while (dir) {
            if (*dir == '\0') {
                dir = strtok_r(NULL, "/", &save);
                continue;
            }

            if (strlen(path) + strlen(dir) + 2 > s) {
                s = strlen(path) + strlen(dir) + 2;
                path = krealloc(path, s);
            }

            strcat(path, "/");
            strcat(path, dir);

            int flags = V_CREATE;
            if (save && *save)
                flags |= V_DIR;

            vnode_t *v;
            kfile_t *f;

            if (vfs_lookup(path, &v) != EOK) {
                char *dup = strdup(path);
                if (flags & V_DIR) {
                    vfs_mkdir(dup, 0755);
                    if (vfs_lookup(dup, &v) != EOK) {
                        return ENOENT;
                    } else {
                        v->gid = file->gid;
                        v->uid = file->uid;
                    }
                } else {
                    if (vfs_create(dup, file->mode) == EOK) {
                        if (vfs_open(dup, 0, &f) == EOK) {
                            kwrite(f, file->data, file->filesize);
                            kclose(f);
                        }
                        if (vfs_lookup(dup, &v) != EOK) {
                            return ENOENT;
                        } else {
                            v->gid = file->gid;
                            v->uid = file->uid;
                        }
                    }
                }
            }

            dir = strtok_r(NULL, "/", &save);
        }

        kfree(name_dup);
    }

    return 0;
}