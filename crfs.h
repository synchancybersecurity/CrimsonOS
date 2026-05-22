#ifndef _CRIMSON_CRFS_H
#define _CRIMSON_CRFS_H

#include <crimson/types.h>

typedef struct block_device {
    char name[32];
    uint64_t size;
    uint32_t block_size;
    int (*read)(struct block_device* d, uint64_t off, void* buf, uint32_t len);
    int (*write)(struct block_device* d, uint64_t off, const void* buf, uint32_t len);
    int (*flush)(struct block_device* d);
    void* private;
} block_device_t;

typedef struct {
    uint32_t inode_no;
    uint16_t rec_len;
    uint8_t  name_len;
    uint8_t  type;
    char     name[0];
} crfs_dirent_t;

typedef struct {
    uint32_t inode_no;
    uint32_t type;
    uint32_t mode;
    uint32_t uid, gid;
    uint64_t size;
    uint64_t blocks;
    uint64_t atime, mtime, ctime;
    uint64_t nlink;
} crfs_inode_disk_t;

#define O_RDONLY    0x0000
#define O_WRONLY    0x0001
#define O_RDWR      0x0002
#define O_CREAT     0x0040
#define O_TRUNC     0x0200
#define O_APPEND    0x0400

#define S_IFREG     0x8000
#define S_IFDIR     0x4000

int crfs_init(void);
int crfs_format(block_device_t* bdev, const char* vol_name);
int crfs_mount(block_device_t* bdev, uint32_t flags);
int crfs_open(const char* path, uint32_t flags, uint32_t mode);
int64_t crfs_read(int fd, void* buf, uint64_t count);
int64_t crfs_write(int fd, const void* buf, uint64_t count);
void crfs_close(int fd);
int crfs_readdir(int fd, crfs_dirent_t* entries, int max);
int crfs_unlink(const char* path);
int crfs_stat(const char* path, crfs_inode_disk_t* out);
void crfs_sync(void);

#endif
