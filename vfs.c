/* Copyright (c) 2026 SynChanCyberSecurity LLC. All Rights Reserved. */
/*
 * Crimson OS - Virtual Filesystem (VFS) Implementation
 * Bridges POSIX-like file operations to CrimsonFS
 */

#include <crimson/types.h>
#include <crimson/fs.h>
#include <crimson/crfs.h>
#include <crimson/string.h>
#include <crimson/spinlock.h>
#include <crimson/printk.h>
#include <crimson/mm.h>

static fs_file_t fd_table[FS_MAX_OPEN];
static spinlock_t vfs_lock;
static int vfs_initialized = 0;

int fs_init(void)
{
    spinlock_init(&vfs_lock);
    memset(fd_table, 0, sizeof(fd_table));

    for (int i = 0; i < FS_MAX_OPEN; i++) {
        fd_table[i].valid = 0;
        fd_table[i].fd = i;
    }

    vfs_initialized = 1;
    return 0;
}

static int alloc_fd(void)
{
    for (int i = 3; i < FS_MAX_OPEN; i++) {  /* 0,1,2 reserved for stdin/out/err */
        if (!fd_table[i].valid) {
            fd_table[i].valid = 1;
            fd_table[i].offset = 0;
            return i;
        }
    }
    return -1;
}

static void free_fd(int fd)
{
    if (fd >= 0 && fd < FS_MAX_OPEN) {
        fd_table[fd].valid = 0;
        fd_table[fd].offset = 0;
        fd_table[fd].node = NULL;
    }
}

int fs_open(const char* path, uint32_t flags)
{
    if (!vfs_initialized || !path) return -1;

    spin_lock(&vfs_lock);

    int fd = alloc_fd();
    if (fd < 0) {
        spin_unlock(&vfs_lock);
        return -1;
    }

    /* Convert VFS flags to CRFS flags */
    uint32_t crfs_flags = 0;
    if (flags & FS_O_RDONLY) crfs_flags |= O_RDONLY;
    if (flags & FS_O_WRONLY) crfs_flags |= O_WRONLY;
    if (flags & FS_O_RDWR)   crfs_flags |= O_RDWR;
    if (flags & FS_O_CREAT)  crfs_flags |= O_CREAT;
    if (flags & FS_O_TRUNC)  crfs_flags |= O_TRUNC;
    if (flags & FS_O_APPEND) crfs_flags |= O_APPEND;

    int crfs_fd = crfs_open(path, crfs_flags, 0644);
    if (crfs_fd < 0) {
        free_fd(fd);
        spin_unlock(&vfs_lock);
        return -1;
    }

    fd_table[fd].flags = flags;
    fd_table[fd].offset = 0;
    fd_table[fd].node = (void*)(uintptr_t)crfs_fd;

    spin_unlock(&vfs_lock);
    return fd;
}

void fs_close(int fd)
{
    if (!vfs_initialized) return;
    if (fd < 0 || fd >= FS_MAX_OPEN || !fd_table[fd].valid) return;

    spin_lock(&vfs_lock);

    int crfs_fd = (int)(uintptr_t)fd_table[fd].node;
    crfs_close(crfs_fd);
    free_fd(fd);

    spin_unlock(&vfs_lock);
}

ssize_t fs_read(int fd, void* buf, size_t count)
{
    if (!vfs_initialized || !buf) return -1;
    if (fd < 0 || fd >= FS_MAX_OPEN || !fd_table[fd].valid) return -1;

    spin_lock(&vfs_lock);

    int crfs_fd = (int)(uintptr_t)fd_table[fd].node;
    int64_t n = crfs_read(crfs_fd, buf, count);
    if (n > 0)
        fd_table[fd].offset += n;

    spin_unlock(&vfs_lock);
    return (ssize_t)n;
}

ssize_t fs_write(int fd, const void* buf, size_t count)
{
    if (!vfs_initialized || !buf) return -1;
    if (fd < 0 || fd >= FS_MAX_OPEN || !fd_table[fd].valid) return -1;

    spin_lock(&vfs_lock);

    int crfs_fd = (int)(uintptr_t)fd_table[fd].node;
    int64_t n = crfs_write(crfs_fd, buf, count);
    if (n > 0)
        fd_table[fd].offset += n;

    spin_unlock(&vfs_lock);
    return (ssize_t)n;
}

off_t fs_lseek(int fd, off_t offset, int whence)
{
    if (!vfs_initialized) return -1;
    if (fd < 0 || fd >= FS_MAX_OPEN || !fd_table[fd].valid) return -1;

    spin_lock(&vfs_lock);

    switch (whence) {
    case FS_SEEK_SET:
        fd_table[fd].offset = (uint64_t)offset;
        break;
    case FS_SEEK_CUR:
        fd_table[fd].offset = (uint64_t)((int64_t)fd_table[fd].offset + offset);
        break;
    case FS_SEEK_END:
        /* Would need stat to implement properly */
        fd_table[fd].offset = (uint64_t)(offset > 0 ? offset : 0);
        break;
    }

    off_t result = (off_t)fd_table[fd].offset;
    spin_unlock(&vfs_lock);
    return result;
}

int fs_fstat(int fd, fs_stat_t* st)
{
    if (!vfs_initialized || !st) return -1;
    if (fd < 0 || fd >= FS_MAX_OPEN || !fd_table[fd].valid) return -1;

    memset(st, 0, sizeof(fs_stat_t));
    /* CrimsonFS doesn't have fstat directly - return defaults */
    st->mode = 0644;
    st->uid = 0;
    st->gid = 0;
    return 0;
}

int fs_stat(const char* path, fs_stat_t* st)
{
    if (!vfs_initialized || !path || !st) return -1;

    crfs_inode_disk_t inode;
    if (crfs_stat(path, &inode) < 0)
        return -1;

    memset(st, 0, sizeof(fs_stat_t));
    st->size = inode.size;
    st->mode = inode.mode;
    st->uid = inode.uid;
    st->gid = inode.gid;
    st->type = (inode.mode & S_IFDIR) ? FS_TYPE_DIR : FS_TYPE_REG;
    st->nlink = (uint32_t)inode.nlink;
    return 0;
}

int fs_mkdir(const char* path, uint32_t mode)
{
    if (!vfs_initialized || !path) return -1;

    /* CrimsonFS mkdir would be implemented in crfs */
    /* For now, return success as placeholder */
    return 0;
}

int fs_mkdir_recursive(const char* path)
{
    if (!vfs_initialized || !path) return -1;

    char tmp[256];
    strncpy(tmp, path, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';

    for (char* p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            fs_mkdir(tmp, 0755);
            *p = '/';
        }
    }
    fs_mkdir(tmp, 0755);
    return 0;
}

int fs_rmdir(const char* path)
{
    if (!vfs_initialized || !path) return -1;
    return 0;  /* Placeholder */
}

int fs_rmdir_recursive(const char* path)
{
    if (!vfs_initialized || !path) return -1;
    /* Would recursively delete all files and subdirectories */
    return 0;  /* Placeholder */
}

int fs_unlink(const char* path)
{
    if (!vfs_initialized || !path) return -1;
    return crfs_unlink(path);
}

int fs_rename(const char* oldpath, const char* newpath)
{
    if (!vfs_initialized || !oldpath || !newpath) return -1;
    return -1;  /* Not implemented in CRFS yet */
}

int fs_chmod(const char* path, uint32_t mode)
{
    if (!vfs_initialized || !path) return -1;
    return 0;  /* Placeholder */
}

int fs_chown(const char* path, uint32_t uid, uint32_t gid)
{
    if (!vfs_initialized || !path) return -1;
    return 0;  /* Placeholder */
}

int fs_exists(const char* path)
{
    if (!vfs_initialized || !path) return 0;

    fs_stat_t st;
    return (fs_stat(path, &st) == 0);
}

int fs_is_dir(const char* path)
{
    if (!vfs_initialized || !path) return 0;

    fs_stat_t st;
    if (fs_stat(path, &st) < 0) return 0;
    return (st.type == FS_TYPE_DIR);
}

int fs_is_file(const char* path)
{
    if (!vfs_initialized || !path) return 0;

    fs_stat_t st;
    if (fs_stat(path, &st) < 0) return 0;
    return (st.type == FS_TYPE_REG);
}
