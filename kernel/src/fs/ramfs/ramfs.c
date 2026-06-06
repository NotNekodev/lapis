#include "ramfs.h"
#include "log/log.h"
#include "util/errno.h"
#include <mm/kheap.h>
#include <util/memory.h>

ramfs_t *ramfs_create_fs(void) {
    ramfs_t *ramfs = kzalloc(sizeof(ramfs_t));
    return ramfs;
}

ramfs_node_t *ramfs_create_node(ramfs_ftype_t ftype) {
    ramfs_node_t *node = kzalloc(sizeof(ramfs_node_t));
    node->type = ftype;

    switch (ftype) {
    case RAMFS_DIRECTORY:
        node->mode = S_IFDIR | 0755;
        break;
    case RAMFS_FILE:
        node->mode = S_IFREG | 0644;
        break;
    case RAMFS_SYMLINK:
        node->mode = S_IFLNK | 0777;
        break;
    }

    return node;
}

int ramfs_find_node(ramfs_t *ramfs, char *path, ramfs_node_t **out) {
    *out = NULL;

    if (!ramfs || !ramfs->root_node || !path) {
        return -1;
    }

    if (path[0] == '/') {
        path++;
    }

    if (path[0] == '\0') {
        *out = ramfs->root_node;
        return 0;
    }

    char *name_dup = strdup(path);
    char *saveptr  = NULL;
    char *dir      = strtok_r(name_dup, "/", &saveptr);

    ramfs_node_t *cur_node = ramfs->root_node->child;

    while (dir) {
        ramfs_node_t *found = NULL;
        for (ramfs_node_t *n = cur_node; n != NULL; n = n->sibling) {
            if (strcmp(n->name, dir) == 0) {
                found = n;
                break;
            }
        }

        if (!found) {
            kfree(name_dup);
            return -1;
        }

        dir = strtok_r(NULL, "/", &saveptr);
        if (dir) {
            cur_node = found->child;
        } else {
            *out = found;
        }
    }

    kfree(name_dup);

    if (!*out) {
        return -1;
    }

    return 0;
}

int ramfs_node_add(ramfs_t *ramfs, char *path, ramfs_node_t **out) {
    if (!ramfs || !ramfs->root_node || !path) {
        return EFAULT;
    }

    if (path[0] == '/') {
        path++;
    }

    char *name_dup = strdup(path);
    char *saveptr  = NULL;
    char *dir      = strtok_r(name_dup, "/", &saveptr);

    ramfs_node_t *cur_node = ramfs->root_node;

    while (dir) {
        char *next_dir = strtok_r(NULL, "/", &saveptr);
        ramfs_ftype_t rt = next_dir ? RAMFS_DIRECTORY : RAMFS_FILE;

        ramfs_node_t *found = NULL;
        for (ramfs_node_t *n = cur_node->child; n != NULL; n = n->sibling) {
            if (strcmp(n->name, dir) == 0 && n->type == rt) {
                found = n;
                break;
            }
        }

        if (!found) {
            ramfs_node_t *n = ramfs_create_node(rt);
            n->name         = strdup(dir);
            ramfs_append_child(cur_node, n);
            found = n;
        }

        if (next_dir) {
            cur_node = found;
        } else {
            *out = found;
        }

        dir = next_dir;
    }

    kfree(name_dup);
    return EOK;
}

int ramfs_find_or_create_node(ramfs_t *ramfs, char *path,
                              ramfs_ftype_t ramfs_ftype, ramfs_node_t **out) {
    ramfs_node_t *found = NULL;
    ramfs_find_node(ramfs, path, &found);

    if (!found) {
        *out = ramfs_create_node(ramfs_ftype);
        return 1;
    }

    *out = found;
    return 0;
}

int ramfs_append_child(ramfs_node_t *parent, ramfs_node_t *child) {
    if (!parent->child) {
        parent->child = child;
        return 0;
    }

    ramfs_node_t *last_child;
    for (last_child = parent->child; last_child->sibling != NULL;
         last_child = last_child->sibling)
        ;
    last_child->sibling = child;

    return 0;
}

int ramfs_print(ramfs_node_t *node, int lvl) {
    if (!node) {
        return -1;
    }

    int a = lvl;

    while (a--) {
        info("  ");
    }

    switch (node->type) {
    case RAMFS_DIRECTORY:
        info("+ %-20s", node->name);
        break;

    case RAMFS_FILE:
        info("  %-20s", node->name);
        info(" | % 10zuB", node->size, node->data);
        break;

    case RAMFS_SYMLINK:
        info("  %-20s -> %s", node->name, (char *)node->data);
        break;
    }

    info("\n");

    if (node->child) {
        ramfs_print(node->child, lvl + 1);
    }

    if (node->sibling) {
        ramfs_print(node->sibling, lvl);
    }
    return 0;
}

size_t ramfs_get_node_size(ramfs_node_t *node) {
    if (!node) {
        return EFAULT;
    }

    size_t s = 0;

    switch (node->type) {
    case RAMFS_FILE:
        s = node->size;
        break;

    case RAMFS_DIRECTORY:
        for (ramfs_node_t *n = node->child; n != NULL; n = n->sibling) {
            s += ramfs_get_node_size(n);
        }
        break;

    case RAMFS_SYMLINK:
        s = strlen((char *)node->data) + 1;
        break;
    }

    return s;
}

int ramfs_open(vnode_t **vnode_r, int flags, bool clone, kfile_t **fio_out) {
    (void)clone;

    vnode_t *vnode = *vnode_r;
    if (!vnode || !vnode_r) {
        return EFAULT;
    }

    ramfs_t *ramfs = vnode->root_vfs->vfs_data;
    if (!ramfs) {
        return EFAULT;
    }

    ramfs_node_t *ramfs_node = NULL;

    if (vnode->node_data) {
        ramfs_node = (ramfs_node_t *)vnode->node_data;
    } else {
        char *rel_path = vnode->path + strlen(vnode->root_vfs->root_vnode->path);
        if (rel_path[0] == '/')
            rel_path++;
        ramfs_find_node(ramfs, rel_path, &ramfs_node);
    }

    if (!ramfs_node) {
        if (!(flags & V_CREATE)) {
            return ENOENT;
        }

        char *rel_path = vnode->path + strlen(vnode->root_vfs->root_vnode->path);
        if (rel_path[0] == '/')
            rel_path++;

        if (ramfs_node_add(ramfs, rel_path, &ramfs_node) != EOK) {
            return ENOTRECOVERABLE;
        }

        if (flags & V_DIR) {
            ramfs_node->type = RAMFS_DIRECTORY;
            ramfs_node->size = 0;
            ramfs_node->data = NULL;
        }
    }

    if (ramfs_node->type == RAMFS_DIRECTORY) {
        vnode->node_data = ramfs_node;
    } else {
        vnode->node_data = ramfs_node;
    }

    kfile_t *fio = *fio_out;
    if (!fio || !fio_out) {
        return EFAULT;
    }

    ramfs_node_t *fio_node = vnode->node_data;
    if (fio_node && fio_node->type != RAMFS_DIRECTORY) {
        fio->buf_start = fio_node->data;
        fio->size      = fio_node->size;
    } else {
        fio->buf_start = NULL;
        fio->size      = 0;
    }
    fio->private   = vnode;

    return EOK;
}

int ramfs_read(vnode_t *vn, size_t *bytes, size_t *offset, void *out) {
    if (!vn) {
        return -EFAULT;
    }

    memset(out, 0, (*bytes));

    ramfs_node_t *ramfs_node = (ramfs_node_t *)vn->node_data;
    if (!ramfs_node) {
        return -EFAULT;
    }

    if ((*bytes) > ramfs_node->size) {
        (*bytes) = ramfs_node->size;
    } else if ((*offset) >= ramfs_node->size) {
        return -EINVAL;
    }

    if ((*bytes) + (*offset) > ramfs_node->size) {
        (*bytes) = (ramfs_node->size - (*offset));
    }

    void *src = (void *)((uintptr_t)ramfs_node->data + (*offset));
    memcpy(out, src, (*bytes));

    return EOK;
}

int ramfs_write(vnode_t *vn, void *buf, size_t *bytes, size_t *offset) {
    if (!vn) {
        return EFAULT;
    }

    ramfs_node_t *ramfs_node = vn->node_data;
    if (!ramfs_node) {
        return EFAULT;
    }

    if ((*bytes) + (*offset) > ramfs_node->size) {
        size_t more = ((*bytes) + (*offset)) - ramfs_node->size;

        void *new_data    = krealloc(ramfs_node->data, ramfs_node->size + more);
        ramfs_node->data  = new_data;
        ramfs_node->size += more;
    }

    void *dst = (void *)((uintptr_t)ramfs_node->data + (*offset));
    memcpy(dst, buf, (*bytes));

    return EOK;
}

int ramfs_close(vnode_t *vnode, int flags, bool clone) {
    (void)flags;
    (void)clone;

    if (!vnode) {
        return EFAULT;
    }

    ramfs_node_t *ramfs_node = vnode->node_data;
    if (!ramfs_node) {
        return EFAULT;
    }

    vnode->node_data = NULL;
    return EOK;
}

int ramfs_ioctl(vnode_t *vnode, int request, void *arg) {
    if (!vnode) {
        return EFAULT;
    }

    (void)request;
    (void)arg;

    return ENOSYS;
}

int ramfs_lookup(vnode_t *parent, const char *name, vnode_t **out) {
    if (!parent || !name || !out) {
        return EFAULT;
    }

    ramfs_t *ramfs = parent->root_vfs->vfs_data;
    if (!ramfs) {
        return EFAULT;
    }

    ramfs_node_t *parent_node = parent->node_data;

    if (!parent_node) {
        char *rel_path = parent->path + strlen(parent->root_vfs->root_vnode->path);
        if (rel_path[0] == '/')
            rel_path++;

        if (rel_path[0] == '\0' ||
            strcmp(parent->path, parent->root_vfs->root_vnode->path) == 0) {
            parent_node = ramfs->root_node;
        } else {
            ramfs_find_node(ramfs, rel_path, &parent_node);
        }
    }

    if (!parent_node) {
        return ENOENT;
    }

    if (parent_node->type != RAMFS_DIRECTORY) {
        return ENOTDIR;
    }

    for (ramfs_node_t *child = parent_node->child; child != NULL;
         child               = child->sibling) {
        if (strcmp(child->name, name) == 0) {

            size_t parent_len = strlen(parent->path);
            size_t child_len  = strlen(name);
            char  *child_path = kmalloc(parent_len + child_len + 2);
            strcpy(child_path, parent->path);
            if (child_path[parent_len - 1] != '/') {
                strcat(child_path, "/");
            }
            strcat(child_path, name);

            vnode_type_t vtype = VNODE_REGULAR;
            if (child->type == RAMFS_DIRECTORY)
                vtype = VNODE_DIR;
            else if (child->type == RAMFS_SYMLINK)
                vtype = VNODE_LINK;

            vnode_t *child_vnode =
                vnode_create(parent->root_vfs, child_path, vtype, child);
            kfree(child_path);
            child_vnode->mode = child->mode;
            memcpy(child_vnode->ops, parent->ops, sizeof(vnops_t));

            *out = child_vnode;
            return EOK;
        }
    }

    return ENOENT;
}

int ramfs_readdir(vnode_t *vnode, dirent_t *entries, size_t *count) {
    if (!vnode || !entries || !count) {
        return EFAULT;
    }

    if (vnode->vtype != VNODE_DIR) {
        return ENOTDIR;
    }

    ramfs_t *ramfs = vnode->root_vfs->vfs_data;
    if (!ramfs) {
        return EFAULT;
    }

    ramfs_node_t *dir_node = vnode->node_data;
    if (!dir_node) {
        char *rel_path = vnode->path + strlen(vnode->root_vfs->root_vnode->path);
        if (rel_path[0] == '/')
            rel_path++;
        if (rel_path[0] == '\0') {
            dir_node = ramfs->root_node;
        } else {
            ramfs_find_node(ramfs, rel_path, &dir_node);
        }
    }

    if (!dir_node || dir_node->type != RAMFS_DIRECTORY) {
        return ENOTDIR;
    }

    size_t idx = 0;
    size_t max = *count;

    for (ramfs_node_t *child = dir_node->child; child != NULL && idx < max;
         child               = child->sibling) {
        entries[idx].d_ino    = (uint64_t)child;
        entries[idx].d_off    = idx + 1;
        entries[idx].d_reclen = sizeof(dirent_t);

        if (child->type == RAMFS_DIRECTORY) {
            entries[idx].d_type = DT_DIR;
        } else if (child->type == RAMFS_SYMLINK) {
            entries[idx].d_type = DT_LNK;
        } else {
            entries[idx].d_type = DT_REG;
        }

        strncpy(entries[idx].d_name, child->name, sizeof(entries[idx].d_name) - 1);
        entries[idx].d_name[sizeof(entries[idx].d_name) - 1] = '\0';

        idx++;
    }

    *count = idx;
    return EOK;
}

int ramfs_readlink(vnode_t *vnode, char *buf, size_t size) {
    if (!vnode || !buf) {
        return EFAULT;
    }

    if (vnode->vtype != VNODE_LINK) {
        return EINVAL;
    }

    ramfs_node_t *link_node = vnode->node_data;
    if (!link_node || link_node->type != RAMFS_SYMLINK) {
        return EINVAL;
    }

    if (!link_node->data) {
        return EINVAL;
    }

    size_t target_len = strlen((char *)link_node->data);
    size_t copy_len   = target_len < size - 1 ? target_len : size - 1;

    memcpy(buf, link_node->data, copy_len);
    buf[copy_len] = '\0';

    return EOK;
}

int ramfs_mkdir(vnode_t *parent, const char *name, int mode) {
    (void)mode;

    if (!parent || !name) {
        return EFAULT;
    }

    ramfs_t *ramfs = parent->root_vfs->vfs_data;
    if (!ramfs) {
        return EFAULT;
    }

    ramfs_node_t *parent_node = parent->node_data;
    if (!parent_node) {
        char *rel_path = parent->path + strlen(parent->root_vfs->root_vnode->path);
        if (rel_path[0] == '/')
            rel_path++;
        if (rel_path[0] == '\0') {
            parent_node = ramfs->root_node;
        } else {
            ramfs_find_node(ramfs, rel_path, &parent_node);
        }
    }

    if (!parent_node || parent_node->type != RAMFS_DIRECTORY) {
        return ENOTDIR;
    }

    for (ramfs_node_t *child = parent_node->child; child != NULL;
         child               = child->sibling) {
        if (strcmp(child->name, name) == 0) {
            return EEXIST;
        }
    }

    ramfs_node_t *new_dir = ramfs_create_node(RAMFS_DIRECTORY);
    new_dir->name         = strdup((char *)name);
    new_dir->mode         = S_IFDIR | (mode & 0777);

    ramfs_append_child(parent_node, new_dir);
    return EOK;
}

int ramfs_rmdir(vnode_t *parent, const char *name) {
    if (!parent || !name) {
        return EFAULT;
    }

    ramfs_t *ramfs = parent->root_vfs->vfs_data;
    if (!ramfs) {
        return EFAULT;
    }

    ramfs_node_t *parent_node = parent->node_data;
    if (!parent_node) {
        char *rel_path = parent->path + strlen(parent->root_vfs->root_vnode->path);
        if (rel_path[0] == '/')
            rel_path++;
        if (rel_path[0] == '\0') {
            parent_node = ramfs->root_node;
        } else {
            ramfs_find_node(ramfs, rel_path, &parent_node);
        }
    }

    if (!parent_node || parent_node->type != RAMFS_DIRECTORY) {
        return ENOTDIR;
    }

    ramfs_node_t **prev = &parent_node->child;
    for (ramfs_node_t *child = parent_node->child; child != NULL;
         prev = &child->sibling, child = child->sibling) {
        if (strcmp(child->name, name) == 0) {
            if (child->type != RAMFS_DIRECTORY) {
                return ENOTDIR;
            }

            if (child->child != NULL) {
                return ENOTEMPTY;
            }

            *prev = child->sibling;
            kfree(child->name);
            kfree(child);
            return EOK;
        }
    }

    return ENOENT;
}

int ramfs_create(vnode_t *parent, const char *name, mode_t mode, vnode_t **out) {
    if (!parent || !name || !out) {
        return EFAULT;
    }

    ramfs_t *ramfs = parent->root_vfs->vfs_data;
    if (!ramfs) {
        return EFAULT;
    }

    ramfs_node_t *parent_node = parent->node_data;
    if (!parent_node) {
        char *rel_path = parent->path + strlen(parent->root_vfs->root_vnode->path);
        if (rel_path[0] == '/')
            rel_path++;
        if (rel_path[0] == '\0') {
            parent_node = ramfs->root_node;
        } else {
            ramfs_find_node(ramfs, rel_path, &parent_node);
        }
    }

    if (!parent_node || parent_node->type != RAMFS_DIRECTORY) {
        return ENOTDIR;
    }

    for (ramfs_node_t *child = parent_node->child; child != NULL;
         child               = child->sibling) {
        if (strcmp(child->name, name) == 0) {
            return EEXIST;
        }
    }

    ramfs_node_t *new_file = ramfs_create_node(RAMFS_FILE);
    new_file->name         = strdup((char *)name);
    new_file->size         = 0;
    new_file->data         = NULL;
    new_file->mode         = S_IFREG | mode;

    ramfs_append_child(parent_node, new_file);

    size_t parent_len = strlen(parent->path);
    size_t name_len   = strlen(name);
    char  *file_path  = kmalloc(parent_len + name_len + 2);
    strcpy(file_path, parent->path);
    if (file_path[parent_len - 1] != '/') {
        strcat(file_path, "/");
    }
    strcat(file_path, name);

    vnode_t *file_vnode =
        vnode_create(parent->root_vfs, file_path, VNODE_REGULAR, new_file);
    kfree(file_path);
    memcpy(file_vnode->ops, parent->ops, sizeof(vnops_t));
    file_vnode->mode = new_file->mode;

    *out = file_vnode;
    return EOK;
}

int ramfs_remove(vnode_t *parent, const char *name) {
    if (!parent || !name) {
        return EFAULT;
    }

    ramfs_t *ramfs = parent->root_vfs->vfs_data;
    if (!ramfs) {
        return EFAULT;
    }

    ramfs_node_t *parent_node = parent->node_data;
    if (!parent_node) {
        char *rel_path = parent->path + strlen(parent->root_vfs->root_vnode->path);
        if (rel_path[0] == '/')
            rel_path++;
        if (rel_path[0] == '\0') {
            parent_node = ramfs->root_node;
        } else {
            ramfs_find_node(ramfs, rel_path, &parent_node);
        }
    }

    if (!parent_node || parent_node->type != RAMFS_DIRECTORY) {
        return ENOTDIR;
    }

    ramfs_node_t **prev = &parent_node->child;
    for (ramfs_node_t *child = parent_node->child; child != NULL;
         prev = &child->sibling, child = child->sibling) {
        if (strcmp(child->name, name) == 0) {
            if (child->type == RAMFS_DIRECTORY) {
                return EISDIR;
            }

            *prev = child->sibling;
            kfree(child->name);
            if (child->data)
                kfree(child->data);
            kfree(child);
            return EOK;
        }
    }

    return ENOENT;
}

int ramfs_symlink(vnode_t *parent, const char *name, const char *target) {
    if (!parent || !name || !target) {
        return EFAULT;
    }

    ramfs_t *ramfs = parent->root_vfs->vfs_data;
    if (!ramfs) {
        return EFAULT;
    }

    ramfs_node_t *parent_node = parent->node_data;
    if (!parent_node) {
        char *rel_path = parent->path + strlen(parent->root_vfs->root_vnode->path);
        if (rel_path[0] == '/')
            rel_path++;
        if (rel_path[0] == '\0') {
            parent_node = ramfs->root_node;
        } else {
            ramfs_find_node(ramfs, rel_path, &parent_node);
        }
    }

    if (!parent_node || parent_node->type != RAMFS_DIRECTORY) {
        return ENOTDIR;
    }

    for (ramfs_node_t *child = parent_node->child; child != NULL;
         child               = child->sibling) {
        if (strcmp(child->name, name) == 0) {
            return EEXIST;
        }
    }

    ramfs_node_t *new_link = ramfs_create_node(RAMFS_SYMLINK);
    new_link->name         = strdup((char *)name);
    new_link->size         = strlen((char *)target) + 1;
    new_link->data         = strdup((char *)target);

    ramfs_append_child(parent_node, new_link);
    return EOK;
}

int ramfs_getattr(vnode_t *vnode, struct stat *st) {
    if (!vnode || !st) {
        return EFAULT;
    }

    ramfs_node_t *ramfs_node = vnode->node_data;
    if (!ramfs_node) {
        ramfs_t *ramfs = vnode->root_vfs->vfs_data;
        char    *rel_path = vnode->path + strlen(vnode->root_vfs->root_vnode->path);
        if (rel_path[0] == '/')
            rel_path++;
        if (rel_path[0] == '\0') {
            ramfs_node = ramfs->root_node;
        } else {
            ramfs_find_node(ramfs, rel_path, &ramfs_node);
        }
    }

    if (!ramfs_node) {
        return EFAULT;
    }

    st->st_dev     = 1;
    st->st_ino     = (uint64_t)ramfs_node;
    st->st_nlink   = 1;
    st->st_mode    = ramfs_node->mode;
    st->st_uid     = vnode->uid;
    st->st_gid     = vnode->gid;
    st->st_rdev    = 0;
    st->st_size    = ramfs_node->size;
    st->st_blksize = 4096;
    st->st_blocks  = (ramfs_node->size + 4095) / 4096;
    st->st_atim    = st->st_mtim = st->st_ctim = (struct timespec){
        .tv_sec  = 0,
        .tv_nsec = 0,
    };

    return EOK;
}

int ramfs_setattr(vnode_t *vnode, struct stat *st) {
    if (!vnode || !st) {
        return EFAULT;
    }

    ramfs_node_t *ramfs_node = vnode->node_data;
    if (!ramfs_node) {
        return EFAULT;
    }

    ramfs_node->mode = st->st_mode;
    vnode->uid       = st->st_uid;
    vnode->gid       = st->st_gid;

    return EOK;
}

vnops_t ramfs_vnops = {
    .open     = ramfs_open,
    .close    = ramfs_close,
    .read     = ramfs_read,
    .write    = ramfs_write,
    .ioctl    = ramfs_ioctl,
    .lookup   = ramfs_lookup,
    .readdir  = ramfs_readdir,
    .readlink = ramfs_readlink,
    .mkdir    = ramfs_mkdir,
    .rmdir    = ramfs_rmdir,
    .create   = ramfs_create,
    .remove   = ramfs_remove,
    .symlink  = ramfs_symlink,
    .getattr  = ramfs_getattr,
    .setattr  = ramfs_setattr,
};

static int ramfs_vfs_mount(vfs_t *vfs, char *path, void *data) {
    (void)path;
    (void)data;

    if (!vfs) {
        return EFAULT;
    }

    return EOK;
}

static int ramfs_vfs_unmount(vfs_t *vfs) {
    if (!vfs) {
        return EFAULT;
    }

    ramfs_t *ramfs = vfs->vfs_data;
    if (ramfs && ramfs->root_node) {
        // TODO: free ramfs tree
    }

    return EOK;
}

static int ramfs_vfs_root(vfs_t *vfs, vnode_t **out) {
    if (!vfs || !out) {
        return EFAULT;
    }

    *out = vfs->root_vnode;
    vnode_ref(*out);

    return EOK;
}

static int ramfs_vfs_statfs(vfs_t *vfs, statfs_t *stat) {
    if (!vfs || !stat) {
        return EFAULT;
    }

    ramfs_t *ramfs = vfs->vfs_data;
    if (!ramfs) {
        return EFAULT;
    }

    stat->block_size   = 1;
    stat->total_blocks = ramfs->ramfs_size;
    stat->free_blocks  = 0;
    stat->total_nodes  = 0;
    stat->free_nodes   = 0;

    return EOK;
}

static int ramfs_vfs_sync(vfs_t *vfs) {
    (void)vfs;
    return EOK;
}

vfsops_t ramfs_vfsops = {
    .mount   = ramfs_vfs_mount,
    .unmount = ramfs_vfs_unmount,
    .root    = ramfs_vfs_root,
    .statfs  = ramfs_vfs_statfs,
    .sync    = ramfs_vfs_sync,
};

static int ramfs_fstype_mount(void *device, char *mount_point, void *mount_data,
                              vfs_t **out) {
    (void)mount_data;

    ramfs_t *ramfs = (ramfs_t *)device;
    if (!ramfs) {
        ramfs                  = ramfs_create_fs();
        ramfs->root_node       = ramfs_create_node(RAMFS_DIRECTORY);
        ramfs->root_node->name = strdup("/");
    }

    if (!ramfs->root_node) {
        ramfs->root_node       = ramfs_create_node(RAMFS_DIRECTORY);
        ramfs->root_node->name = strdup("/");
    }

    vfs_t *vfs = vfs_create_fs(&ramfs_fstype, ramfs);
    if (!vfs) {
        return ENOMEM;
    }

    memcpy(vfs->ops, &ramfs_vfsops, sizeof(vfsops_t));

    vfs->root_vnode = vnode_create(vfs, mount_point, VNODE_DIR, ramfs->root_node);
    if (!vfs->root_vnode) {
        kfree(vfs->ops);
        kfree(vfs);
        return ENOMEM;
    }
    
    // Ensure root_vnode path is always set to mount_point
    if (vfs->root_vnode->path) {
        kfree(vfs->root_vnode->path);
    }
    vfs->root_vnode->path = strdup(mount_point);

    memcpy(vfs->root_vnode->ops, &ramfs_vnops, sizeof(vnops_t));
    vfs->root_vnode->mode = ramfs->root_node->mode;
    *out = vfs;
    return EOK;
}

vfs_fs_driver_t ramfs_fstype = {
    .id = 0, .name = "ramfs", .mount = ramfs_fstype_mount, .next = NULL};

void ramfs_init(void) {
    vfs_register_driver(&ramfs_fstype);
}

int ramfs_vfs_init(ramfs_t *ramfs, char *path) {
    if (!path) {
        return EFAULT;
    }

    vfs_t *vfs = vfs_mount(ramfs, "ramfs", path, NULL);
    if (!vfs) {
        return ENOTRECOVERABLE;
    }

    return EOK;
}