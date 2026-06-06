#include "fs/fsid.h"
#include "stdbool.h"
#include "util/spinlock.h"
#include "util/types.h"
#include "util/time.h"
#include <stddef.h>
#include "kfile.h"
#ifndef _VFS_H
#define _VFS_H 1

#define V_CREATE (1 << 0)
#define V_READ   (1 << 1)
#define V_WRITE  (1 << 2)
#define V_EXCL   (1 << 3)
#define V_TRUNC  (1 << 4)
#define V_DIR    (1 << 5)

#define S_IRUSR 0400
#define S_IWUSR 0200
#define S_IXUSR 0100

#define S_IRGRP 0040
#define S_IWGRP 0020
#define S_IXGRP 0010

#define S_IROTH 0004
#define S_IWOTH 0002
#define S_IXOTH 0001

#define	S_IFDIR	 0040000	/* Directory.  */
#define	S_IFCHR	 0020000	/* Character device.  */
#define	S_IFBLK	 0060000	/* Block device.  */
#define	S_IFREG	 0100000	/* Regular file.  */
#define	S_IFIFO	 0010000	/* FIFO.  */
#define	S_IFLNK	 0120000	/* Symbolic link.  */
#define	S_IFSOCK 0140000	/* Socket.  */

#define S_IFMT	 0170000	/* Type of file.  */

#define S_ISUID  0x800
#define S_ISGID  0x400

typedef struct vnode vnode_t;
typedef struct vfs vfs_t;

typedef struct vfs_fs_driver {
    uint16_t id;
    char name[32];

    int (*mount)(void *device, char *mount_point, void *mount_data,
                 vfs_t **out);

    struct vfs_fs_driver *next;
} vfs_fs_driver_t;

typedef enum vnode_type {
    VNODE_NULL,
    VNODE_REGULAR,
    VNODE_DIR,
    VNODE_BLOCK,
    VNODE_CHAR,
    VNODE_LINK,
    VNODE_PIPE,
    VNODE_SOCKET,
    VNODE_BAD,
} vnode_type_t;

#define DT_UNKNOWN 0
#define DT_FIFO    1
#define DT_CHR     2
#define DT_DIR     4
#define DT_BLK     6
#define DT_REG     8
#define DT_LNK     10
#define DT_SOCK    12
#define DT_WHT     14

static inline uint8_t vnode_type_to_dtype(vnode_type_t vt) {
    switch (vt) {
    case VNODE_REGULAR: return DT_REG;
    case VNODE_DIR:     return DT_DIR;
    case VNODE_BLOCK:   return DT_BLK;
    case VNODE_CHAR:    return DT_CHR;
    case VNODE_LINK:    return DT_LNK;
    case VNODE_PIPE:    return DT_FIFO;
    case VNODE_SOCKET:  return DT_SOCK;
    default:            return DT_UNKNOWN;
    }
}

typedef struct statfs {
    fsid_t fsid;

    uint64_t block_size;
    uint64_t total_blocks;
    uint64_t free_blocks;

    uint64_t total_nodes;
    uint64_t free_nodes;
} statfs_t;

typedef struct stat {
	uint64_t st_dev;
	uint64_t st_ino;
	unsigned long st_nlink;
	mode_t st_mode;
	uid_t st_uid;
	gid_t st_gid;
	unsigned int __pad0;
	uint64_t st_rdev;
	off_t st_size;
	long st_blksize;
	int64_t st_blocks;
	timespec_t st_atim;
	timespec_t st_mtim;
	timespec_t st_ctim;
	long __unused[3];
} stat_t;

typedef struct fid {
    size_t fid_len;
    char *fid_data;
} fid_t;

typedef struct dirent {
    uint64_t d_ino;
    int64_t d_off;
    uint16_t d_reclen;
    uint8_t d_type;
    char d_name[256];
} __attribute__((packed)) dirent_t;

typedef struct vfs_ops {
    int (*mount)(vfs_t *, char *, void *);
    int (*unmount)(vfs_t *);
    int (*root)(vfs_t *, vnode_t **);
    int (*statfs)(vfs_t *, statfs_t *);
    int (*sync)(vfs_t *);
    int (*fid)(vfs_t *, vnode_t *, fid_t **);
    int (*vget)(vfs_t *, vnode_t **, fid_t *);
} vfsops_t;

typedef struct vnode_ops {
    int (*open)(vnode_t **, int, bool, kfile_t **);
    int (*close)(vnode_t *, int, bool);

    int (*read)(vnode_t *, size_t *, size_t *, void *);
    int (*write)(vnode_t *, void *, size_t *, size_t *);
    int (*ioctl)(vnode_t *, int, void *);
    int (*lookup)(vnode_t *, const char *, vnode_t **);
    int (*readdir)(vnode_t *, dirent_t *, size_t *);
    int (*readlink)(vnode_t *, char *, size_t);
    int (*mkdir)(vnode_t *, const char *, int);
    int (*rmdir)(vnode_t *, const char *);
    int (*create)(vnode_t *, const char *, mode_t, vnode_t **);
    int (*remove)(vnode_t *, const char *);
    int (*symlink)(vnode_t *, const char *, const char *);
    int (*mmap)(vnode_t *, void *, size_t, int, int, size_t);
    int (*getattr)(vnode_t *, stat_t *);
    int (*setattr)(vnode_t *, stat_t *);
} vnops_t;

typedef struct vnode {
    // TODO: flags
    // TODO: Unix IPC , Stream (shrugs)
    // TODO: shared/exclusive locks

    char *path;
    vnode_type_t vtype;
    void *node_data; // FS-specific structure about the file

    uid_t uid;
    gid_t gid;
    mode_t mode;

    vnops_t *ops;

    vfs_t *vfs_here; // what vfs is mounted in this vnode
    vfs_t *root_vfs; // in what vfs this vnode resides

    uint32_t refcount;

    spinlock_t vnode_lock;
} vnode_t;

typedef struct vfs {
    vfs_fs_driver_t *driver;
    vnode_t *root_vnode;
    void *vfs_data; 

    size_t block_size;
    int flags;

    spinlock_t vfs_lock;

    vfsops_t *ops;

    struct vfs *next; // next VFS
} vfs_t;

extern vfs_t *vfs_list_head;

int vfs_register_driver(vfs_fs_driver_t *driver); // returns the unique ID of the driver, or -error on failure (see util/errno.h)
int vfs_unregister_driver(uint16_t id);
vfs_fs_driver_t *vfs_find_driver_by_name(const char *name);
vfs_fs_driver_t *vfs_find_driver_by_id(uint16_t id);

vfs_t *vfs_create_fs(vfs_fs_driver_t *driver, void *fs_data);
vfs_t *vfs_mount(void *device, const char *fstype_name, char *path, void *mount_data);
int vfs_unmount(const char *path);
int vfs_append(vfs_t *vfs);


vnode_t *vnode_create(vfs_t *root_vfs, char *path, vnode_type_t type, void *data);
void vnode_ref(vnode_t *vnode);
void vnode_unref(vnode_t *vnode);

int vfs_resolve_mount(const char *path, vfs_t **out, char **remaining_path);
int vfs_lookup(const char *path, vnode_t **out);
int vfs_lookup_parent(const char *path, vnode_t **parent, char **filename);

int vfs_open(const char *path, int flags, kfile_t **out);
int vfs_read(vnode_t *vnode, size_t size, size_t offset, void *out);
int vfs_write(vnode_t *vnode, void *buf, size_t size, size_t offset);
int vfs_ioctl(vnode_t *vnode, int request, void *arg);
int vfs_close(vnode_t *vnode);

int vfs_readdir(vnode_t *vnode, dirent_t *entries, size_t *count);
int vfs_mkdir(const char *path, int mode);
int vfs_create(const char *path, mode_t mode);
int vfs_rmdir(const char *path);
int vfs_remove(const char *path);

int vfs_readlink(const char *path, char *buf, size_t size);
int vfs_symlink(const char *target, const char *linkpath);

int vfs_stat(const char *path, stat_t *st);
int vfs_setstat(const char *path, stat_t *st);

#endif // _VFS_H