/* Copyright (c) 2026 SynChanCyberSecurity LLC. All Rights Reserved. */
/*
 * Crimson OS - Virtual Filesystem (VFS) Layer
 * Provides POSIX-like file operations on top of CrimsonFS
 */

#ifndef _CRIMSON_FS_H
#define _CRIMSON_FS_H

#include <crimson/types.h>
#include <crimson/crfs.h>

/* File descriptor flags */
#define FS_O_RDONLY     0x0000
#define FS_O_WRONLY     0x0001
#define FS_O_RDWR       0x0002
#define FS_O_CREAT      0x0040
#define FS_O_TRUNC      0x0200
#define FS_O_APPEND     0x0400
#define FS_O_NONBLOCK   0x0800

/* Seek origins */
#define FS_SEEK_SET     0
#define FS_SEEK_CUR     1
#define FS_SEEK_END     2

/* File types */
#define FS_TYPE_REG     1
#define FS_TYPE_DIR     2
#define FS_TYPE_CHR     3
#define FS_TYPE_BLK     4
#define FS_TYPE_FIFO    5

/* Maximum open files */
#define FS_MAX_OPEN     256

/* File descriptor table entry */
typedef struct fs_file {
    int         valid;
    int         fd;
    uint32_t    flags;
    uint64_t    offset;
    void*       node;       /* Filesystem-specific node */
} fs_file_t;

/* Filesystem stat structure */
typedef struct fs_stat {
    uint64_t    size;
    uint32_t    mode;
    uint32_t    uid;
    uint32_t    gid;
    uint64_t    atime;
    uint64_t    mtime;
    uint64_t    ctime;
    uint32_t    type;
    uint32_t    nlink;
} fs_stat_t;

/* VFS operations */
int     fs_init(void);
int     fs_open(const char* path, uint32_t flags);
void    fs_close(int fd);
ssize_t fs_read(int fd, void* buf, size_t count);
ssize_t fs_write(int fd, const void* buf, size_t count);
off_t   fs_lseek(int fd, off_t offset, int whence);
int     fs_fstat(int fd, fs_stat_t* st);
int     fs_stat(const char* path, fs_stat_t* st);

/* Directory operations */
int     fs_mkdir(const char* path, uint32_t mode);
int     fs_mkdir_recursive(const char* path);
int     fs_rmdir(const char* path);
int     fs_rmdir_recursive(const char* path);
int     fs_unlink(const char* path);
int     fs_rename(const char* oldpath, const char* newpath);

/* File attributes */
int     fs_chmod(const char* path, uint32_t mode);
int     fs_chown(const char* path, uint32_t uid, uint32_t gid);

/* Path operations */
int     fs_exists(const char* path);
int     fs_is_dir(const char* path);
int     fs_is_file(const char* path);

#endif /* _CRIMSON_FS_H */
