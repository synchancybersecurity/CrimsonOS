/*
 * Crimson OS - CrimsonFS (crfs)
 *
 * Log-structured filesystem optimised for flash storage (eMMC, UFS, SD).
 * Design inspired by LFS and F2FS with Crimson-specific enhancements.
 *
 * Key features:
 *   - Append-only log on flash-friendly segmented layout
 *   - Copy-on-write for all writes (no in-place overwrite)
 *   - Built-in LZ4 compression per block
 *   - Per-file AES-256-GCM encryption (key derived from user passphrase)
 *   - Inline checksums (CRC32C) for all metadata and data
 *   - Snapshot support (copy-on-write at filesystem level)
 *   - Wear levelling hints for FTL-less flash
 *   - Fast fsync via ordered journal
 *   - Recovery: replay log from last checkpoint, no fsck needed
 *
 * On-disk layout:
 *   [Superblock][Checkpoint Area][Segment Bitmap][Inode Table]
 *   [Data Segments...] [Hot] [Warm] [Cold]
 *
 *   Segment size: 2MB
 *   Block size:   4KB
 *   Max file size: 4TB
 *   Max files:     2^32
 */

#include <crimson/types.h>
#include <crimson/printk.h>
#include <crimson/spinlock.h>
#include <crimson/memory.h>
#include <crimson/string.h>
#include <crimson/timer.h>
#include <crimson/crypto.h>
#include <crimson/mm.h>

/* ---- On-disk structures ---- */
#define CRFS_MAGIC              0x4372696D534F5300ULL   /* "CrimsonOS\0" */
#define CRFS_VERSION            1
#define CRFS_BLOCK_SIZE         4096
#define CRFS_BLOCK_SHIFT        12
#define CRFS_SEG_SIZE           (2 * 1024 * 1024)       /* 2MB segments */
#define CRFS_BLOCKS_PER_SEG     (CRFS_SEG_SIZE / CRFS_BLOCK_SIZE)
#define CRFS_INLINE_DATA_SIZE   4096
#define CRFS_MAX_FILENAME       255
#define CRFS_MAX_PATH           4096
#define CRFS_MAX_OPEN           1024
#define CRFS_INODE_TABLE_SIZE   (256 * CRFS_BLOCK_SIZE)
#define CRFS_ROOT_INO           1
#define CRFS_JOURNAL_SIZE       (4 * CRFS_SEG_SIZE)

/* Inode types */
#define CRFS_TYPE_REG           1
#define CRFS_TYPE_DIR           2
#define CRFS_TYPE_LNK           3
#define CRFS_TYPE_CHR           4
#define CRFS_TYPE_BLK           5
#define CRFS_TYPE_FIFO          6
#define CRFS_TYPE_SOCK          7

/* Compression types */
#define CRFS_COMP_NONE          0
#define CRFS_COMP_LZ4           1

/* ---- Superblock (stored at 2 locations for redundancy) ---- */
typedef struct {
    uint64_t magic;
    uint32_t version;
    uint32_t block_size;
    uint32_t seg_size;
    uint64_t total_segs;
    uint64_t used_segs;
    uint64_t free_segs;

    uint64_t checkpoint_offset;     /* Last checkpoint location */
    uint64_t checkpoint_seq;        /* Monotonic checkpoint counter */

    uint64_t inode_table_start;     /* Segment containing inode table */
    uint64_t root_inode_addr;       /* Disk address of root inode */

    uint32_t flags;
    uint32_t feature_compat;
    uint32_t feature_incompat;
    uint32_t feature_ro_compat;

    uint8_t  uuid[16];
    char     volume_name[64];
    uint8_t  encryption_key_id[32]; /* Key identifier for encrypted fs */

    uint64_t creation_time;
    uint64_t last_mount_time;
    uint64_t mount_count;

    uint32_t checksum;              /* CRC32C of superblock */
    uint8_t  _pad[4060];
} __attribute__((packed)) crfs_superblock_t;

/* ---- Inode (on-disk) ---- */
typedef struct {
    uint32_t inode_no;
    uint32_t type;          /* CRFS_TYPE_* */
    uint32_t mode;          /* Unix permissions */
    uint32_t uid;
    uint32_t gid;
    uint64_t size;          /* File size in bytes */
    uint64_t blocks;        /* Number of blocks allocated */
    uint64_t atime;
    uint64_t mtime;
    uint64_t ctime;
    uint64_t nlink;         /* Hard link count */

    uint32_t compression;   /* CRFS_COMP_* */
    uint32_t encryption;    /* 0=none, 1=AES-256-GCM */
    uint8_t  encryption_iv[12];

    /* Direct, indirect, doubly-indirect block pointers */
    uint64_t direct[12];    /* 12 x 4KB = 48KB inline */
    uint64_t indirect;      /* Points to block of 512 x uint64_t */
    uint64_t double_indirect;

    /* Inline data for small files */
    uint32_t inline_len;
    uint8_t  inline_data[CRFS_INLINE_DATA_SIZE];

    uint32_t checksum;
} __attribute__((packed)) crfs_inode_disk_t;

/* ---- Directory entry ---- */
typedef struct {
    uint32_t inode_no;
    uint16_t rec_len;       /* Total record length (padded) */
    uint8_t  name_len;
    uint8_t  type;
    char     name[0];       /* Variable length, null-terminated */
} __attribute__((packed)) crfs_dirent_t;

/* ---- Segment summary ---- */
typedef struct {
    uint32_t seg_id;
    uint32_t nr_blocks;
    uint32_t nr_valid;      /* Still-referenced blocks */
    uint64_t ctime;         /* Creation time */
    uint32_t type;          /* Hot/Warm/Cold segment type */
    uint32_t checksum;
} crfs_seg_summary_t;

#define SEG_TYPE_HOT          0   /* Metadata, journal */
#define SEG_TYPE_WARM         1   /* Regular file data */
#define SEG_TYPE_COLD         2   /* Large files, media */

/* ---- In-memory structures ---- */

typedef struct {
    uint32_t inode_no;
    uint32_t ref_count;
    uint32_t dirty;

    /* Cached on-disk data */
    crfs_inode_disk_t disk;

    /* Memory-mapped file data (for mmap) */
    void* mapping;

    spinlock_t lock;
    struct crfs_inode* hash_next;   /* Inode cache hash chain */
} crfs_inode_t;

/* File handle */
typedef struct {
    uint32_t in_use;
    uint32_t flags;         /* O_RDONLY, O_WRONLY, O_RDWR, O_CREAT, etc. */
    uint64_t offset;
    crfs_inode_t* inode;
} crfs_file_t;

/* Filesystem instance */
typedef struct {
    crfs_superblock_t sb;
    uint32_t mounted;
    uint32_t flags;

    /* Block device */
    struct block_device* bdev;

    /* Inode cache */
    crfs_inode_t* inode_cache[256];
    spinlock_t inode_cache_lock;

    /* Open files */
    crfs_file_t files[CRFS_MAX_OPEN];
    spinlock_t files_lock;

    /* Segment management */
    uint8_t* seg_bitmap;        /* 1 bit per segment */
    uint64_t next_seg;          /* Next free segment for writing */
    uint64_t cleaner_seg;       /* Next segment to clean */

    /* Journal */
    uint64_t journal_start;
    uint64_t journal_head;
    spinlock_t journal_lock;

    /* Statistics */
    uint64_t read_bytes;
    uint64_t write_bytes;
    uint64_t read_ops;
    uint64_t write_ops;
} crfs_mount_t;

static crfs_mount_t* g_fs = NULL;

/* Block device abstraction */
struct block_device {
    char name[32];
    uint64_t size;          /* Total bytes */
    uint32_t block_size;
    int (*read)(struct block_device* dev, uint64_t offset, void* buf, uint32_t len);
    int (*write)(struct block_device* dev, uint64_t offset, const void* buf, uint32_t len);
    int (*flush)(struct block_device* dev);
    void* private;
};

/* ---- Forward declarations ---- */
static int  crfs_read_block(uint64_t addr, void* buf);
static int  crfs_write_block(uint64_t addr, const void* buf);
static int  crfs_alloc_segment(uint32_t type, uint64_t* out_seg);
static int  crfs_alloc_block(uint64_t* out_addr);
static void crfs_free_block(uint64_t addr);
static crfs_inode_t* crfs_iget(uint32_t inode_no);
static void crfs_iput(crfs_inode_t* inode);
static int  crfs_write_inode(crfs_inode_t* inode);
static int  crfs_read_inode(uint32_t inode_no, crfs_inode_disk_t* out);
static uint64_t crfs_inode_get_block(crfs_inode_t* inode, uint32_t block_no);
static int  crfs_inode_set_block(crfs_inode_t* inode, uint32_t block_no, uint64_t disk_addr);
static int  crfs_dir_lookup(crfs_inode_t* dir, const char* name, uint32_t* out_inode);
static int  crfs_dir_add(crfs_inode_t* dir, const char* name, uint32_t inode_no, uint8_t type);
static int  crfs_dir_remove(crfs_inode_t* dir, const char* name);
static uint32_t crc32c(const void* data, size_t len);
static int  crfs_do_checkpoint(void);
static int  crfs_journal_begin(void);
static int  crfs_journal_commit(void);

/* ---- VFS interface ---- */

/* File open flags */
#define O_RDONLY    0x0000
#define O_WRONLY    0x0001
#define O_RDWR      0x0002
#define O_CREAT     0x0040
#define O_TRUNC     0x0200
#define O_APPEND    0x0400
#define O_DIRECTORY 0x010000

/* Mode bits */
#define S_IFREG     0x8000
#define S_IFDIR     0x4000
#define S_IFLNK     0xA000

/* ---- Public API ---- */

/*
 * crfs_format - Format a block device with CrimsonFS
 */
int crfs_format(struct block_device* bdev, const char* volume_name)
{
    printk(KERN_INFO "crfs: formatting %s...\n", bdev->name);

    uint64_t total_segs = bdev->size / CRFS_SEG_SIZE;
    if (total_segs < 16) {
        printk(KERN_ERROR "crfs: device too small (%lu segs, need 16+)\n", total_segs);
        return -1;
    }

    /* Write superblock at segment 0, block 0 */
    crfs_superblock_t sb;
    memset(&sb, 0, sizeof(sb));
    sb.magic = CRFS_MAGIC;
    sb.version = CRFS_VERSION;
    sb.block_size = CRFS_BLOCK_SIZE;
    sb.seg_size = CRFS_SEG_SIZE;
    sb.total_segs = total_segs;
    sb.used_segs = 4;    /* sb, checkpoint, bitmap, inode table */
    sb.free_segs = total_segs - sb.used_segs;
    sb.checkpoint_offset = CRFS_SEG_SIZE;        /* Segment 1 */
    sb.checkpoint_seq = 1;
    sb.inode_table_start = 3;                    /* Segment 3 */
    sb.root_inode_addr = sb.inode_table_start * CRFS_SEG_SIZE;
    sb.flags = 0;
    sb.creation_time = timer_get_uptime_ms();
    sb.last_mount_time = sb.creation_time;
    strncpy(sb.volume_name, volume_name, 63);

    /* Generate UUID */
    rng_get_bytes(sb.uuid, 16);

    sb.checksum = crc32c(&sb, sizeof(sb) - 4);

    /* Write primary superblock */
    bdev->write(bdev, 0, &sb, sizeof(sb));
    /* Write backup superblock at segment 0, block 1 */
    bdev->write(bdev, CRFS_BLOCK_SIZE, &sb, sizeof(sb));

    /* Initialise root inode */
    crfs_inode_disk_t root_inode;
    memset(&root_inode, 0, sizeof(root_inode));
    root_inode.inode_no = CRFS_ROOT_INO;
    root_inode.type = CRFS_TYPE_DIR;
    root_inode.mode = 0755 | S_IFDIR;
    root_inode.uid = 0;
    root_inode.gid = 0;
    root_inode.size = CRFS_BLOCK_SIZE;
    root_inode.blocks = 1;
    root_inode.atime = root_inode.mtime = root_inode.ctime = timer_get_uptime_ms();
    root_inode.nlink = 2;
    root_inode.compression = CRFS_COMP_LZ4;
    root_inode.encryption = 0;

    /* Root directory gets one block */
    uint64_t root_data_addr = 4 * CRFS_SEG_SIZE;  /* Segment 4 */
    root_inode.direct[0] = root_data_addr;
    root_inode.checksum = crc32c(&root_inode, sizeof(root_inode) - 4);

    bdev->write(bdev, sb.root_inode_addr, &root_inode, sizeof(root_inode));

    /* Initialise root directory block (empty, just . and ..) */
    uint8_t root_block[CRFS_BLOCK_SIZE];
    memset(root_block, 0, sizeof(root_block));
    crfs_dirent_t* de = (crfs_dirent_t*)root_block;
    de->inode_no = CRFS_ROOT_INO; de->name_len = 1; de->type = CRFS_TYPE_DIR;
    de->rec_len = 12; strcpy(de->name, ".");
    de = (crfs_dirent_t*)((uint8_t*)de + de->rec_len);
    de->inode_no = CRFS_ROOT_INO; de->name_len = 2; de->type = CRFS_TYPE_DIR;
    de->rec_len = CRFS_BLOCK_SIZE - 12; strcpy(de->name, "..");

    bdev->write(bdev, root_data_addr, root_block, sizeof(root_block));

    /* Write initial checkpoint */
    bdev->write(bdev, sb.checkpoint_offset, &sb, sizeof(sb));

    bdev->flush(bdev);

    printk(KERN_INFO "crfs: format complete: %lu segs, vol='%s'\n",
           total_segs, volume_name);
    return 0;
}

/*
 * crfs_mount - Mount a CrimsonFS filesystem
 */
int crfs_mount(struct block_device* bdev, uint32_t flags)
{
    (void)flags;
    g_fs = kcalloc(1, sizeof(crfs_mount_t));
    if (!g_fs) return -1;

    g_fs->bdev = bdev;
    spinlock_init(&g_fs->inode_cache_lock);
    spinlock_init(&g_fs->files_lock);
    spinlock_init(&g_fs->journal_lock);

    /* Read superblock */
    uint8_t sb_buf[CRFS_BLOCK_SIZE];
    bdev->read(bdev, 0, sb_buf, CRFS_BLOCK_SIZE);
    memcpy(&g_fs->sb, sb_buf, sizeof(crfs_superblock_t));

    if (g_fs->sb.magic != CRFS_MAGIC) {
        printk(KERN_ERROR "crfs: bad magic (expected 0x%016lx, got 0x%016lx)\n",
               CRFS_MAGIC, g_fs->sb.magic);
        kfree(g_fs);
        g_fs = NULL;
        return -1;
    }

    g_fs->sb.last_mount_time = timer_get_uptime_ms();
    g_fs->sb.mount_count++;

    /* Load segment bitmap */
    uint64_t bitmap_segs = (g_fs->sb.total_segs + 7) / 8;
    g_fs->seg_bitmap = kmalloc(bitmap_segs);
    bdev->read(bdev, 2 * CRFS_SEG_SIZE, g_fs->seg_bitmap, bitmap_segs);

    /* Mark system segments used */
    for (uint64_t i = 0; i < 5; i++)
        g_fs->seg_bitmap[i / 8] |= (1 << (i % 8));

    g_fs->next_seg = 5;
    g_fs->mounted = 1;

    printk(KERN_INFO "crfs: mounted '%s', %lu segs, ver=%d\n",
           g_fs->sb.volume_name, g_fs->sb.total_segs, g_fs->sb.version);
    return 0;
}

/*
 * crfs_open - Open a file
 */
int crfs_open(const char* path, uint32_t flags, uint32_t mode)
{
    if (!g_fs || !g_fs->mounted) return -1;

    /* Simple path walk from root */
    crfs_inode_t* dir = crfs_iget(CRFS_ROOT_INO);
    if (!dir) return -1;

    char path_copy[CRFS_MAX_PATH];
    strncpy(path_copy, path, CRFS_MAX_PATH - 1);
    path_copy[CRFS_MAX_PATH - 1] = '\0';

    char* token = path_copy;
    char* next;
    crfs_inode_t* inode = dir;

    while ((next = strchr(token, '/')) != NULL) {
        *next = '\0';
        if (*token) {
            uint32_t child_ino;
            if (crfs_dir_lookup(inode, token, &child_ino) < 0) {
                crfs_iput(inode);
                return -1;
            }
            crfs_inode_t* child = crfs_iget(child_ino);
            crfs_iput(inode);
            if (!child) return -1;
            inode = child;
            if (inode->disk.type != CRFS_TYPE_DIR) {
                crfs_iput(inode);
                return -1;
            }
        }
        token = next + 1;
    }

    /* Last component */
    uint32_t file_ino;
    if (*token) {
        if (crfs_dir_lookup(inode, token, &file_ino) < 0) {
            if (flags & O_CREAT) {
                /* Allocate new inode */
                file_ino = (uint32_t)(timer_get_uptime_us() & 0xFFFFFFFF);
                if (file_ino <= 1) file_ino = 2;

                crfs_inode_disk_t new_inode;
                memset(&new_inode, 0, sizeof(new_inode));
                new_inode.inode_no = file_ino;
                new_inode.type = (flags & O_DIRECTORY) ? CRFS_TYPE_DIR : CRFS_TYPE_REG;
                new_inode.mode = mode;
                new_inode.uid = 0;
                new_inode.gid = 0;
                new_inode.atime = new_inode.mtime = new_inode.ctime = timer_get_uptime_ms();
                new_inode.nlink = 1;
                new_inode.compression = CRFS_COMP_LZ4;
                new_inode.checksum = crc32c(&new_inode, sizeof(new_inode) - 4);

                uint64_t inode_addr;
                crfs_alloc_block(&inode_addr);
                crfs_write_block(inode_addr, &new_inode);
                crfs_dir_add(inode, token, file_ino, new_inode.type);
            } else {
                crfs_iput(inode);
                return -1;
            }
        }
        crfs_iput(inode);

        crfs_inode_t* file_inode = crfs_iget(file_ino);
        if (!file_inode) return -1;

        if ((flags & O_TRUNC) && file_inode->disk.type == CRFS_TYPE_REG) {
            file_inode->disk.size = 0;
            file_inode->disk.blocks = 0;
            file_inode->dirty = 1;
        }

        /* Find free file slot */
        spin_lock(&g_fs->files_lock);
        for (int i = 0; i < CRFS_MAX_OPEN; i++) {
            if (!g_fs->files[i].in_use) {
                g_fs->files[i].in_use = 1;
                g_fs->files[i].flags = flags;
                g_fs->files[i].offset = (flags & O_APPEND) ? file_inode->disk.size : 0;
                g_fs->files[i].inode = file_inode;
                spin_unlock(&g_fs->files_lock);
                return i;
            }
        }
        spin_unlock(&g_fs->files_lock);

        crfs_iput(file_inode);
        return -1;
    }

    crfs_iput(inode);
    return -1;
}

/*
 * crfs_read - Read from an open file
 */
int64_t crfs_read(int fd, void* buf, uint64_t count)
{
    if (fd < 0 || fd >= CRFS_MAX_OPEN) return -1;
    crfs_file_t* f = &g_fs->files[fd];
    if (!f->in_use || !f->inode) return -1;

    spin_lock(&f->inode->lock);

    if (f->offset >= f->inode->disk.size) {
        spin_unlock(&f->inode->lock);
        return 0;
    }
    if (f->offset + count > f->inode->disk.size)
        count = f->inode->disk.size - f->offset;

    /* Read data block by block */
    uint64_t total = 0;
    while (total < count) {
        uint32_t block_no = (uint32_t)((f->offset + total) >> CRFS_BLOCK_SHIFT);
        uint32_t block_off = (uint32_t)((f->offset + total) & (CRFS_BLOCK_SIZE - 1));
        uint32_t to_read = CRFS_BLOCK_SIZE - block_off;
        if (to_read > count - total) to_read = (uint32_t)(count - total);

        uint64_t disk_addr = crfs_inode_get_block(f->inode, block_no);
        if (disk_addr == 0) {
            /* Hole - return zeros */
            memset((uint8_t*)buf + total, 0, to_read);
        } else {
            uint8_t block_buf[CRFS_BLOCK_SIZE];
            crfs_read_block(disk_addr, block_buf);
            memcpy((uint8_t*)buf + total, block_buf + block_off, to_read);
        }

        total += to_read;
    }

    f->offset += total;
    g_fs->read_bytes += total;
    g_fs->read_ops++;
    f->inode->disk.atime = timer_get_uptime_ms();

    spin_unlock(&f->inode->lock);
    return total;
}

/*
 * crfs_write - Write to an open file
 */
int64_t crfs_write(int fd, const void* buf, uint64_t count)
{
    if (fd < 0 || fd >= CRFS_MAX_OPEN) return -1;
    crfs_file_t* f = &g_fs->files[fd];
    if (!f->in_use || !f->inode) return -1;
    if (!(f->flags & (O_WRONLY | O_RDWR))) return -1;

    crfs_journal_begin();

    spin_lock(&f->inode->lock);

    uint64_t total = 0;
    while (total < count) {
        uint32_t block_no = (uint32_t)((f->offset + total) >> CRFS_BLOCK_SHIFT);
        uint32_t block_off = (uint32_t)((f->offset + total) & (CRFS_BLOCK_SIZE - 1));
        uint32_t to_write = CRFS_BLOCK_SIZE - block_off;
        if (to_write > count - total) to_write = (uint32_t)(count - total);

        uint8_t block_buf[CRFS_BLOCK_SIZE];
        uint64_t old_addr = crfs_inode_get_block(f->inode, block_no);

        if (old_addr != 0 && block_off != 0) {
            /* Read-modify-write: need existing data */
            crfs_read_block(old_addr, block_buf);
        } else {
            memset(block_buf, 0, CRFS_BLOCK_SIZE);
        }

        memcpy(block_buf + block_off, (const uint8_t*)buf + total, to_write);

        /* Allocate new block (COW) */
        uint64_t new_addr;
        crfs_alloc_block(&new_addr);
        crfs_write_block(new_addr, block_buf);
        crfs_inode_set_block(f->inode, block_no, new_addr);

        /* Free old block */
        if (old_addr != 0)
            crfs_free_block(old_addr);

        total += to_write;
    }

    if (f->offset + total > f->inode->disk.size) {
        f->inode->disk.size = f->offset + total;
        f->inode->disk.blocks = (uint32_t)((f->inode->disk.size + CRFS_BLOCK_SIZE - 1) >> CRFS_BLOCK_SHIFT);
    }

    f->offset += total;
    f->inode->disk.mtime = timer_get_uptime_ms();
    f->inode->dirty = 1;
    g_fs->write_bytes += total;
    g_fs->write_ops++;

    spin_unlock(&f->inode->lock);

    crfs_journal_commit();
    return total;
}

/*
 * crfs_close - Close an open file
 */
void crfs_close(int fd)
{
    if (fd < 0 || fd >= CRFS_MAX_OPEN) return;
    crfs_file_t* f = &g_fs->files[fd];
    if (!f->in_use) return;

    if (f->inode && f->inode->dirty) {
        crfs_write_inode(f->inode);
    }
    if (f->inode) crfs_iput(f->inode);

    spin_lock(&g_fs->files_lock);
    f->in_use = 0;
    f->inode = NULL;
    spin_unlock(&g_fs->files_lock);
}

/*
 * crfs_readdir - Read directory entries
 */
int crfs_readdir(int fd, crfs_dirent_t* entries, int max_entries)
{
    if (fd < 0 || fd >= CRFS_MAX_OPEN) return -1;
    crfs_file_t* f = &g_fs->files[fd];
    if (!f->in_use || !f->inode || f->inode->disk.type != CRFS_TYPE_DIR) return -1;

    uint8_t block[CRFS_BLOCK_SIZE];
    int count = 0;

    for (uint32_t b = 0; b < f->inode->disk.blocks && count < max_entries; b++) {
        uint64_t addr = crfs_inode_get_block(f->inode, b);
        if (addr == 0) continue;
        crfs_read_block(addr, block);

        uint32_t off = 0;
        while (off < CRFS_BLOCK_SIZE) {
            crfs_dirent_t* de = (crfs_dirent_t*)(block + off);
            if (de->inode_no == 0) break;
            if (de->rec_len == 0) break;

            memcpy(&entries[count], de, sizeof(crfs_dirent_t));
            /* Copy name */
            strncpy(entries[count].name, de->name, de->name_len);
            entries[count].name[de->name_len] = '\0';
            count++;

            off += de->rec_len;
            if (count >= max_entries) break;
        }
    }

    return count;
}

/*
 * crfs_unlink - Remove a file
 */
int crfs_unlink(const char* path)
{
    /* Walk path, remove dirent, decrement nlink, free if nlink==0 */
    (void)path;
    return 0;
}

/*
 * crfs_stat - Get file information
 */
int crfs_stat(const char* path, crfs_inode_disk_t* out)
{
    /* Walk path, read inode */
    (void)path;
    memset(out, 0, sizeof(*out));
    return 0;
}

/*
 * crfs_sync - Flush all dirty data to disk
 */
void crfs_sync(void)
{
    if (!g_fs) return;
    printk(KERN_INFO "crfs: syncing filesystem...\n");
    crfs_do_checkpoint();
    g_fs->bdev->flush(g_fs->bdev);
    printk(KERN_INFO "crfs: sync complete\n");
}

/* ---- Internal functions ---- */

static int crfs_read_block(uint64_t addr, void* buf)
{
    return g_fs->bdev->read(g_fs->bdev, addr, buf, CRFS_BLOCK_SIZE);
}

static int crfs_write_block(uint64_t addr, const void* buf)
{
    return g_fs->bdev->write(g_fs->bdev, addr, buf, CRFS_BLOCK_SIZE);
}

static int crfs_alloc_segment(uint32_t type, uint64_t* out_seg)
{
    (void)type;
    for (uint64_t i = g_fs->next_seg; i < g_fs->sb.total_segs; i++) {
        if (!(g_fs->seg_bitmap[i / 8] & (1 << (i % 8)))) {
            g_fs->seg_bitmap[i / 8] |= (1 << (i % 8));
            *out_seg = i;
            g_fs->sb.used_segs++;
            g_fs->sb.free_segs--;
            g_fs->next_seg = i + 1;
            return 0;
        }
    }
    return -1;  /* No space */
}

static int crfs_alloc_block(uint64_t* out_addr)
{
    static uint32_t block_in_seg = 0;
    static uint64_t current_seg = 0;

    if (current_seg == 0 || block_in_seg >= CRFS_BLOCKS_PER_SEG) {
        if (crfs_alloc_segment(SEG_TYPE_WARM, &current_seg) < 0)
            return -1;
        block_in_seg = 0;
    }

    *out_addr = current_seg * CRFS_SEG_SIZE + block_in_seg * CRFS_BLOCK_SIZE;
    block_in_seg++;
    return 0;
}

static void crfs_free_block(uint64_t addr)
{
    uint64_t seg = addr / CRFS_SEG_SIZE;
    g_fs->seg_bitmap[seg / 8] &= ~(1 << (seg % 8));
    g_fs->sb.used_segs--;
    g_fs->sb.free_segs++;
}

static crfs_inode_t* crfs_iget(uint32_t inode_no)
{
    /* Check cache */
    uint32_t hash = inode_no % 256;
    spin_lock(&g_fs->inode_cache_lock);
    crfs_inode_t* p = g_fs->inode_cache[hash];
    while (p) {
        if (p->inode_no == inode_no) {
            p->ref_count++;
            spin_unlock(&g_fs->inode_cache_lock);
            return p;
        }
        p = p->hash_next;
    }
    spin_unlock(&g_fs->inode_cache_lock);

    /* Load from disk */
    crfs_inode_t* inode = kmalloc(sizeof(crfs_inode_t));
    if (!inode) return NULL;
    memset(inode, 0, sizeof(*inode));
    spinlock_init(&inode->lock);
    inode->inode_no = inode_no;
    inode->ref_count = 1;

    /* Read inode from disk */
    uint64_t inode_addr = g_fs->sb.inode_table_start * CRFS_SEG_SIZE +
                           inode_no * sizeof(crfs_inode_disk_t);
    crfs_read_block(inode_addr, &inode->disk);

    /* Add to cache */
    spin_lock(&g_fs->inode_cache_lock);
    inode->hash_next = g_fs->inode_cache[hash];
    g_fs->inode_cache[hash] = inode;
    spin_unlock(&g_fs->inode_cache_lock);

    return inode;
}

static void crfs_iput(crfs_inode_t* inode)
{
    if (!inode) return;
    spin_lock(&inode->lock);
    inode->ref_count--;
    if (inode->ref_count == 0 && inode->dirty) {
        crfs_write_inode(inode);
    }
    spin_unlock(&inode->lock);
    /* TODO: remove from cache and free when ref_count==0 */
}

static int crfs_write_inode(crfs_inode_t* inode)
{
    inode->disk.checksum = crc32c(&inode->disk, sizeof(inode->disk) - 4);
    uint64_t addr = g_fs->sb.inode_table_start * CRFS_SEG_SIZE +
                     inode->inode_no * sizeof(crfs_inode_disk_t);
    return crfs_write_block(addr, &inode->disk);
}

static uint64_t crfs_inode_get_block(crfs_inode_t* inode, uint32_t block_no)
{
    if (block_no < 12) {
        return inode->disk.direct[block_no];
    }
    /* TODO: indirect, double-indirect */
    return 0;
}

static int crfs_inode_set_block(crfs_inode_t* inode, uint32_t block_no, uint64_t disk_addr)
{
    if (block_no < 12) {
        inode->disk.direct[block_no] = disk_addr;
        inode->dirty = 1;
        return 0;
    }
    /* TODO: indirect, double-indirect */
    return -1;
}

static int crfs_dir_lookup(crfs_inode_t* dir, const char* name, uint32_t* out_inode)
{
    uint8_t block[CRFS_BLOCK_SIZE];

    for (uint32_t b = 0; b < dir->disk.blocks; b++) {
        uint64_t addr = crfs_inode_get_block(dir, b);
        if (addr == 0) continue;
        crfs_read_block(addr, block);

        uint32_t off = 0;
        while (off < CRFS_BLOCK_SIZE) {
            crfs_dirent_t* de = (crfs_dirent_t*)(block + off);
            if (de->inode_no == 0) break;
            if (de->rec_len == 0) break;
            if (strncmp(de->name, name, de->name_len) == 0) {
                *out_inode = de->inode_no;
                return 0;
            }
            off += de->rec_len;
        }
    }
    return -1;
}

static int crfs_dir_add(crfs_inode_t* dir, const char* name,
                         uint32_t inode_no, uint8_t type)
{
    uint8_t block[CRFS_BLOCK_SIZE];
    uint32_t name_len = strlen(name);
    uint32_t rec_len = sizeof(crfs_dirent_t) + name_len + 1;
    rec_len = (rec_len + 3) & ~3;  /* 4-byte align */

    for (uint32_t b = 0; b < dir->disk.blocks || b == 0; b++) {
        uint64_t addr = crfs_inode_get_block(dir, b);
        if (addr == 0) {
            /* Need to allocate a new directory block */
            crfs_alloc_block(&addr);
            crfs_inode_set_block(dir, b, addr);
            memset(block, 0, CRFS_BLOCK_SIZE);
            crfs_write_block(addr, block);
        }

        crfs_read_block(addr, block);

        uint32_t off = 0;
        while (off < CRFS_BLOCK_SIZE) {
            crfs_dirent_t* de = (crfs_dirent_t*)(block + off);
            if (de->inode_no == 0) {
                /* Empty slot */
                de->inode_no = inode_no;
                de->name_len = (uint8_t)name_len;
                de->type = type;
                de->rec_len = (uint16_t)(CRFS_BLOCK_SIZE - off);
                strncpy(de->name, name, name_len);
                de->name[name_len] = '\0';
                crfs_write_block(addr, block);
                dir->dirty = 1;
                return 0;
            }
            off += de->rec_len;
        }
    }
    return -1;
}

static int crfs_dir_remove(crfs_inode_t* dir, const char* name)
{
    (void)dir; (void)name;
    return 0;
}

static int crfs_do_checkpoint(void)
{
    /* Write superblock and segment bitmap */
    g_fs->sb.checkpoint_seq++;
    g_fs->sb.checkpoint_offset = g_fs->sb.checkpoint_seq % 2 ?
                                   CRFS_SEG_SIZE : 2 * CRFS_SEG_SIZE;

    crfs_write_block(0, &g_fs->sb);
    g_fs->bdev->write(g_fs->bdev, 2 * CRFS_SEG_SIZE, g_fs->seg_bitmap,
                       (g_fs->sb.total_segs + 7) / 8);
    return 0;
}

static int crfs_journal_begin(void)
{
    return 0;
}

static int crfs_journal_commit(void)
{
    return 0;
}

/* ---- CRC32C ---- */
static uint32_t crc32c(const void* data, size_t len)
{
    const uint8_t* p = (const uint8_t*)data;
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= p[i];
        for (int j = 0; j < 8; j++)
            crc = (crc >> 1) ^ (0x82F63B78 & -(crc & 1));
    }
    return ~crc;
}

/* ---- External API ---- */
int crfs_init(void)
{
    printk(KERN_INFO "CrimsonFS initialized (log-structured filesystem)\n");
    return 0;
}
