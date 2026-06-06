#include "kfile.h"
#include "log/log.h"
#include "mm/kheap.h"
#include "util/errno.h"
#include "vfs.h"
#include <log/nanoprintf.h>
#include <util/memory.h>

int f2vflags(int fio_flags) {
    int vflags = 0;
    if (fio_flags & (O_CREATE)) {
        vflags |= V_CREATE;
    }
    if (fio_flags & (O_DIRECTORY)) {
        vflags |= V_DIR;
    }

    return vflags;
}

kfile_t *kfile_create(void) {
    kfile_t *file = kzalloc(sizeof(kfile_t));
    if (!file) {
        return NULL;
    }

    file->buf_start = NULL;
    file->size      = 0;
    file->offset    = 0;
    file->flags     = 0;
    file->private   = NULL;

    return file;
}

int kopen(const char *path, int flags, mode_t mode, kfile_t **out) {
    if (!path || !out)
        return -EINVAL;

    if (flags & O_CREAT) {
        int res = vfs_create(path, mode);
        if (res != EOK && res != -EEXIST) {
            return res;
        }
    }

    kfile_t *f  = NULL;
    int      ret = vfs_open(path, f2vflags(flags & ~O_CREAT), &f);
    if (ret != EOK) {
        return ret;
    }

    f->offset = 0;
    f->flags  = flags;

    *out = f;
    return EOK;
}

size_t kread(kfile_t *file, void *out, size_t size) {
    if (!file || !out) {
        return -EFAULT;
    }

    if (file->flags & PIPE_READ_END) {
        error("tried to read from a pipe, but pipes arent implemented!\n");
        return -ENOSYS;
    } else if (file->flags & PIPE_WRITE_END) {
        return 0;
    }

    size_t bytes  = size;
    size_t offset = file->offset;

    int ret = ((vnode_t *)file->private)->ops->read(((vnode_t *)file->private), &bytes, &offset, out);

    if (ret != EOK) {
        return -ret;
    }

    file->offset += bytes;
    return bytes;
}

int kwrite(kfile_t *file, void *buf, size_t size) {
    if (!file || !buf) {
        return -EFAULT;
    }

    if (file->flags & PIPE_WRITE_END) {
        error("tried to write to a pipe, but pipes arent implemented!\n");
        return -ENOSYS;
    } else if (file->flags & PIPE_READ_END) {
        return 0;
    }

    size_t offset = file->offset;
    if (file->flags & O_APPEND) {
        offset = file->size;
    }

    vnode_t *vn = file->private;
    int ret = vfs_write(vn, buf, size, offset);

    if (ret < 0) {
        return ret;
    }

    if (file->offset + size > file->size) {
        file->size = file->offset + size;
    }

    file->offset += size;
    return ret;
}

int kclose(kfile_t *kfile) {
    if (!kfile) {
        return -EFAULT;
    }

    if (kfile->flags & PIPE_READ_END) {
        return 0;
    } else if (kfile->flags & PIPE_WRITE_END) {
        return 0;
    }

    int ret = vfs_close((vnode_t *)kfile->private);
    kfree(kfile);

    return ret;
}

size_t kseek(kfile_t *file, off_t offset, fseek_t whence) {
    if (!file) {
        return -EFAULT;
    }

    switch (whence) {
    case SEEK_CUR:
        file->offset += offset;
        break;
    case SEEK_END:
        file->offset = (file->size + offset);
        break;
    case SEEK_SET:
        file->offset = offset;
        break;
    default:
        break;
    }

    return file->offset;
}

size_t kfcntl(kfile_t *file, fcntl_cmd_t cmd, void *arg) {
    switch (cmd) {
    case F_GETFL:
        return file->flags;
    case F_SETFL: {
        size_t flags  = *(size_t *)arg;
        file->flags  |= (flags & (O_APPEND));
        break;
    }
    default:
        break;
    }

    return EOK;
}

const char *file_type_char(mode_t mode) {
    mode_t type = mode & S_IFMT;

    if (type == S_IFREG)  return "-";
    if (type == S_IFDIR)  return "d";
    if (type == S_IFLNK)  return "l";
    if (type == S_IFCHR)  return "c";
    if (type == S_IFBLK)  return "b";
    if (type == S_IFIFO)  return "p";
    if (type == S_IFSOCK) return "s";
    return "?";
}

void mode_to_string(mode_t mode, char *str) {
    str[0] = file_type_char(mode)[0];

    str[1] = (mode & S_IRUSR) ? 'r' : '-';
    str[2] = (mode & S_IWUSR) ? 'w' : '-';
    str[3] = (mode & S_IXUSR) ? 'x' : '-';

    str[4] = (mode & S_IRGRP) ? 'r' : '-';
    str[5] = (mode & S_IWGRP) ? 'w' : '-';
    str[6] = (mode & S_IXGRP) ? 'x' : '-';

    str[7] = (mode & S_IROTH) ? 'r' : '-';
    str[8] = (mode & S_IWOTH) ? 'w' : '-';
    str[9] = (mode & S_IXOTH) ? 'x' : '-';

    str[10] = '\0';
}

static void fs_list_internal(vnode_t *dir, int depth, int max_depth, int indent) {
    if (max_depth != -1 && depth > max_depth) {
        return;
    }

    if (!dir || dir->vtype != VNODE_DIR) {
        return;
    }

    dirent_t entries[256];
    size_t   count = 256;
    char     mode_buf[11];

    int ret = vfs_readdir(dir, entries, &count);
    if (ret != EOK) {
        return;
    }

    for (size_t i = 0; i < count; i++) {
        for (int j = 0; j < indent; j++) {
            info("  ");
        }

        size_t path_buf_len = strlen(dir->path) + strlen(entries[i].d_name) + 2;
        char  *path_buf     = kmalloc(path_buf_len);
        npf_snprintf(path_buf, path_buf_len, "%s/%s", dir->path, entries[i].d_name);

        strcpy(mode_buf, "??????????");

        vnode_t *vnode = NULL;
        int      res   = vfs_lookup(path_buf, &vnode);
        if (res == EOK && vnode) {
            mode_to_string(vnode->mode, mode_buf);
            vnode_unref(vnode);
        }

        kfree(path_buf);

        switch (entries[i].d_type) {
            case DT_DIR:
                info("|- [%s] %s/\n", mode_buf, entries[i].d_name);
                break;
            case DT_LNK: {
                size_t full_path_len = strlen(dir->path) + strlen(entries[i].d_name) + 2;
                char  *full_path     = kmalloc(full_path_len);
                npf_snprintf(full_path, full_path_len, "%s/%s", dir->path, entries[i].d_name);

                char target[256];
                int  lret = vfs_readlink(full_path, target, sizeof(target));
                if (lret == EOK) {
                    info("|- [%s] %s -> %s\n", mode_buf, entries[i].d_name, target);
                } else {
                    info("|- [%s] %s -> ??? (%d)\n", mode_buf, entries[i].d_name, lret);
                }

                kfree(full_path);
                break;
            }
            default:
                info("|- [%s] %s\n", mode_buf, entries[i].d_name);
                break;
        }

        if (entries[i].d_type == DT_DIR) {
            if (strcmp(entries[i].d_name, ".") == 0 ||
                strcmp(entries[i].d_name, "..") == 0) {
                continue;
            }

            size_t path_len  = strlen(dir->path) + strlen(entries[i].d_name) + 2;
            char  *child_path = kmalloc(path_len);
            if (strcmp(dir->path, "/") == 0) {
                npf_snprintf(child_path, path_len, "/%s", entries[i].d_name);
            } else {
                npf_snprintf(child_path, path_len, "%s/%s", dir->path, entries[i].d_name);
            }

            vnode_t *child_vnode;
            if (vfs_lookup(child_path, &child_vnode) == EOK) {
                fs_list_internal(child_vnode, depth + 1, max_depth, indent + 1);
                vnode_unref(child_vnode);
            }
            kfree(child_path);
        }
    }
}

int fs_list(const char *path, int max_depth) {
    if (!path) {
        return -EINVAL;
    }

    vnode_t *vnode;
    int ret = vfs_lookup(path, &vnode);
    if (ret != EOK) {
        info("Error: Cannot access '%s': %d\n", path, ret);
        return ret;
    }

    if (vnode->vtype != VNODE_DIR) {
        info("Error: '%s' is not a directory\n", path);
        vnode_unref(vnode);
        return -ENOTDIR;
    }

    info("%s\n", path);
    fs_list_internal(vnode, 0, max_depth, 0);

    vnode_unref(vnode);
    return EOK;
}