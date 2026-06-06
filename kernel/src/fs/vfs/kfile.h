#include <stddef.h>
#ifndef _FILE_H
#define _FILE_H 1

#include "util/types.h"

typedef enum fseek {
    SEEK_SET = 0,
    SEEK_CUR = 1,
    SEEK_END = 2
} fseek_t;

typedef enum fcntl_cmd {
    F_GETFL = 0,
    F_SETFL = 1,
} fcntl_cmd_t;

#define O_RDONLY    0x0000
#define O_WRONLY    0x0001
#define O_RDWR      0x0002
#define O_ACCMODE   0x0003
#define O_CREAT     0x0040
#define O_EXCL      0x0080
#define O_NOCTTY    0x0100
#define O_TRUNC     0x0200
#define O_APPEND    0x0400
#define O_NONBLOCK  0x0800
#define O_DSYNC     0x1000
#define O_SYNC      0x101000
#define O_DIRECTORY 0x10000
#define O_NOFOLLOW  0x20000
#define O_CLOEXEC   0x80000
#define O_CREATE    O_CREAT

#define PIPE_READ_END  (1 << 20)
#define PIPE_WRITE_END (1 << 21)

#define SPECIAL_kfile_tYPE_PIPE   (1 << 4)
#define SPECIAL_kfile_tYPE_DEVICE (1 << 5)

typedef struct kfile {
    void   *buf_start;
    size_t  size;
    size_t  flags;
    size_t  offset;
    void   *private;
} kfile_t;

typedef struct vnode   vnode_t;
typedef struct dirent  dirent_t;

typedef struct dir_handle {
    vnode_t  *vnode;
    dirent_t *entries;
    size_t    count;
    size_t    index;
    size_t    syscall_ret_num;
} dir_handle_t;

kfile_t *kfile_create(void);
int      kopen(const char *path, int flags, mode_t mode, kfile_t **out);
size_t   kread(kfile_t *file, void *buf, size_t size);
int      kwrite(kfile_t *file, void *buf, size_t size);
int      kclose(kfile_t *file);
size_t   kseek(kfile_t *file, off_t offset, fseek_t whence);
size_t   kfcntl(kfile_t *file, fcntl_cmd_t cmd, void *arg);
int      fs_list(const char *path, int max_depth);

#endif // _FILE_H