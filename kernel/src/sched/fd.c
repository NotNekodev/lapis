#include <sched/task.h>
#include <mm/kheap.h>
#include <util/memory.h>
#include <util/errno.h>
#include <log/log.h>
#include <fs/vfs/vfs.h>
#include <fs/vfs/kfile.h>
#include <stddef.h>

fd_table_t *fdtable_create(void) {
    fd_table_t *table = kzalloc(sizeof(fd_table_t));
    if (!table) {
        return NULL;
    }

    table->entries = kzalloc(sizeof(fd_entry_t *) * FD_TABLE_INITIAL);
    if (!table->entries) {
        kfree(table);
        return NULL;
    }

    table->capacity = FD_TABLE_INITIAL;
    table->refcount = 1;
    table->lock = (spinlock_t)SPINLOCK_INIT("fdtable_lock");

    return table;
}

void fdtable_ref(fd_table_t *table) {
    if (!table) {
        return;
    }
    spinlock_acquire(&table->lock);
    table->refcount++;
    spinlock_release(&table->lock);
}

void fd_entry_ref(fd_entry_t *entry) {
    if (!entry) {
        return;
    }
    spinlock_acquire(&entry->lock);
    entry->refcount++;
    spinlock_release(&entry->lock);
}

void fd_entry_unref(fd_entry_t *entry) {
    if (!entry) {
        return;
    }

    spinlock_acquire(&entry->lock);
    entry->refcount--;
    uint32_t remaining = entry->refcount;
    spinlock_release(&entry->lock);

    if (remaining > 0) {
        return;
    }

    if (entry->type == FD_FILE && entry->file) {
        kclose(entry->file);
    } else if (entry->type == FD_DIR) {
        if (entry->dir_vnode) {
            vnode_unref(entry->dir_vnode);
        }
        if (entry->dir_buf) {
            kfree(entry->dir_buf);
        }
    }

    kfree(entry);
}

void fdtable_unref(fd_table_t *table) {
    if (!table) {
        return;
    }

    spinlock_acquire(&table->lock);
    table->refcount--;
    uint32_t remaining = table->refcount;
    spinlock_release(&table->lock);

    if (remaining > 0) {
        return;
    }

    for (size_t i = 0; i < table->capacity; i++) {
        if (table->entries[i]) {
            fd_entry_unref(table->entries[i]);
        }
    }

    kfree(table->entries);
    kfree(table);
}

static int fdtable_grow(fd_table_t *table, size_t min_capacity) {
    if (min_capacity > FD_TABLE_MAX) {
        return -EMFILE;
    }

    size_t new_capacity = table->capacity * 2;
    if (new_capacity < min_capacity) {
        new_capacity = min_capacity;
    }
    if (new_capacity > FD_TABLE_MAX) {
        new_capacity = FD_TABLE_MAX;
    }

    fd_entry_t **new_entries = kzalloc(sizeof(fd_entry_t *) * new_capacity);
    if (!new_entries) {
        return -ENOMEM;
    }

    memcpy(new_entries, table->entries, sizeof(fd_entry_t *) * table->capacity);
    kfree(table->entries);
    table->entries = new_entries;
    table->capacity = new_capacity;

    return EOK;
}

int fdtable_install_at(fd_table_t *table, int fd, fd_entry_t *entry) {
    if (!table || fd < 0 || !entry) {
        return -EINVAL;
    }

    spinlock_acquire(&table->lock);

    if ((size_t)fd >= table->capacity) {
        int ret = fdtable_grow(table, (size_t)fd + 1);
        if (ret != EOK) {
            spinlock_release(&table->lock);
            return ret;
        }
    }

    if (table->entries[fd]) {
        spinlock_release(&table->lock);
        return -EBUSY;
    }

    table->entries[fd] = entry;
    fd_entry_ref(entry);

    spinlock_release(&table->lock);
    return fd;
}

int fdtable_install(fd_table_t *table, fd_entry_t *entry) {
    if (!table || !entry) {
        return -EINVAL;
    }

    spinlock_acquire(&table->lock);

    for (size_t i = 0; i < table->capacity; i++) {
        if (!table->entries[i]) {
            table->entries[i] = entry;
            fd_entry_ref(entry);
            spinlock_release(&table->lock);
            return (int)i;
        }
    }

    size_t fd = table->capacity;
    int ret = fdtable_grow(table, table->capacity + 1);
    if (ret != EOK) {
        spinlock_release(&table->lock);
        return ret;
    }

    table->entries[fd] = entry;
    fd_entry_ref(entry);

    spinlock_release(&table->lock);
    return (int)fd;
}

fd_entry_t *fdtable_get(fd_table_t *table, int fd) {
    if (!table || fd < 0) {
        return NULL;
    }

    spinlock_acquire(&table->lock);

    fd_entry_t *entry = NULL;
    if ((size_t)fd < table->capacity) {
        entry = table->entries[fd];
        if (entry) {
            fd_entry_ref(entry);
        }
    }

    spinlock_release(&table->lock);
    return entry;
}

int fdtable_close(fd_table_t *table, int fd) {
    if (!table || fd < 0) {
        return -EINVAL;
    }

    spinlock_acquire(&table->lock);

    if ((size_t)fd >= table->capacity || !table->entries[fd]) {
        spinlock_release(&table->lock);
        return -EBADF;
    }

    fd_entry_t *entry = table->entries[fd];
    table->entries[fd] = NULL;

    spinlock_release(&table->lock);

    fd_entry_unref(entry);
    return EOK;
}

int fdtable_dup(fd_table_t *table, int oldfd) {
    fd_entry_t *entry = fdtable_get(table, oldfd);
    if (!entry) {
        return -EBADF;
    }

    int newfd = fdtable_install(table, entry);
    fd_entry_unref(entry);

    return newfd;
}

int fdtable_dup2(fd_table_t *table, int oldfd, int newfd) {
    if (newfd < 0) {
        return -EINVAL;
    }

    fd_entry_t *entry = fdtable_get(table, oldfd);
    if (!entry) {
        return -EBADF;
    }

    if (oldfd == newfd) {
        fd_entry_unref(entry);
        return newfd;
    }

    fdtable_close(table, newfd);

    int ret = fdtable_install_at(table, newfd, entry);
    fd_entry_unref(entry);

    if (ret < 0) {
        return ret;
    }

    return newfd;
}

fd_table_t *fdtable_clone(fd_table_t *src) {
    if (!src) {
        return fdtable_create();
    }

    fd_table_t *dst = kzalloc(sizeof(fd_table_t));
    if (!dst) {
        return NULL;
    }

    spinlock_acquire(&src->lock);

    dst->entries = kzalloc(sizeof(fd_entry_t *) * src->capacity);
    if (!dst->entries) {
        spinlock_release(&src->lock);
        kfree(dst);
        return NULL;
    }

    dst->capacity = src->capacity;
    dst->refcount = 1;
    dst->lock = (spinlock_t)SPINLOCK_INIT("fdtable_lock");

    for (size_t i = 0; i < src->capacity; i++) {
        if (src->entries[i]) {
            dst->entries[i] = src->entries[i];
            fd_entry_ref(dst->entries[i]);
        }
    }

    spinlock_release(&src->lock);
    return dst;
}

fd_entry_t *fd_open(const char *path, int flags, mode_t mode) {
    if (!path) {
        return NULL;
    }

    fd_entry_t *entry = kzalloc(sizeof(fd_entry_t));
    if (!entry) {
        return NULL;
    }

    entry->lock = (spinlock_t)SPINLOCK_INIT("fd_entry_lock");
    entry->refcount = 1;
    entry->flags = flags;

    if (flags & O_DIRECTORY) {
        vnode_t *vnode = NULL;
        int ret = vfs_lookup(path, &vnode);
        if (ret != EOK) {
            kfree(entry);
            return NULL;
        }

        if (vnode->vtype != VNODE_DIR) {
            vnode_unref(vnode);
            kfree(entry);
            return NULL;
        }

        entry->type = FD_DIR;
        entry->dir_vnode = vnode;
        entry->dir_buf = NULL;
        entry->dir_count = 0;
        entry->dir_index = 0;
        entry->dir_off = 0;

        return entry;
    }

    vnode_t *probe = NULL;
    if (vfs_lookup(path, &probe) == EOK) {
        if (probe->vtype == VNODE_DIR) {
            vnode_unref(probe);
            kfree(entry);
            return NULL;
        }
        vnode_unref(probe);
    }

    kfile_t *file = NULL;
    int ret = kopen(path, flags, mode, &file);
    if (ret != EOK) {
        kfree(entry);
        return NULL;
    }

    entry->type = FD_FILE;
    entry->file = file;

    return entry;
}

size_t fd_read(fd_entry_t *entry, void *buf, size_t size) {
    if (!entry || entry->type != FD_FILE) {
        return (size_t)-EINVAL;
    }
    return kread(entry->file, buf, size);
}

int fd_write(fd_entry_t *entry, void *buf, size_t size) {
    if (!entry || entry->type != FD_FILE) {
        return -EINVAL;
    }
    return kwrite(entry->file, buf, size);
}

size_t fd_seek(fd_entry_t *entry, off_t offset, fseek_t whence) {
    if (!entry || entry->type != FD_FILE) {
        return (size_t)-EINVAL;
    }
    return kseek(entry->file, offset, whence);
}

int fd_readdir(fd_entry_t *entry, dirent_t *out, size_t max) {
    if (!entry || entry->type != FD_DIR || !out || max == 0) {
        return -EINVAL;
    }

    spinlock_acquire(&entry->lock);

    if (!entry->dir_buf) {
        dirent_t *buf = kmalloc(sizeof(dirent_t) * 256);
        if (!buf) {
            spinlock_release(&entry->lock);
            return -ENOMEM;
        }

        size_t count = 256;
        int ret = vfs_readdir(entry->dir_vnode, buf, &count);
        if (ret != EOK) {
            kfree(buf);
            spinlock_release(&entry->lock);
            return ret;
        }

        entry->dir_buf = buf;
        entry->dir_count = count;
        entry->dir_index = 0;
    }

    if (entry->dir_index >= entry->dir_count) {
        spinlock_release(&entry->lock);
        return 0;
    }

    memcpy(out, &entry->dir_buf[entry->dir_index], sizeof(dirent_t));
    entry->dir_index++;

    spinlock_release(&entry->lock);
    return 1;
}
