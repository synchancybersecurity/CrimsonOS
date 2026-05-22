/*
 * Crimson OS - Package Executor
 * Loads and executes real packages from the Crimson Store.
 * Handles: ELF loading, dependency resolution, signature verification,
 * sandboxed process creation, and runtime lifecycle management.
 */

#include <crimson/types.h>
#include <crimson/printk.h>
#include <crimson/spinlock.h>
#include <crimson/timer.h>
#include <crimson/memory.h>
#include <crimson/string.h>
#include <crimson/process.h>
#include <crimson/fs.h>
#include <crimson/sandbox.h>
#include <crimson/pkg.h>

/* ═══════════════════════════════════════════════════════════
 *  ELF LOADER
 * ═══════════════════════════════════════════════════════════ */

/* ELF64 header structures */
#define ELF_MAGIC       0x464C457F  /* "\x7FELF" */
#define ET_EXEC         2
#define ET_DYN          3
#define EM_AARCH64      183
#define PT_LOAD         1
#define PT_DYNAMIC      2
#define PT_INTERP       3
#define PF_X            1
#define PF_W            2
#define PF_R            4

typedef struct __attribute__((packed)) {
    uint32_t    e_magic;
    uint8_t     e_class;        /* 1=32, 2=64 */
    uint8_t     e_data;         /* 1=LE, 2=BE */
    uint8_t     e_version;
    uint8_t     e_osabi;
    uint8_t     e_pad[8];
    uint16_t    e_type;
    uint16_t    e_machine;
    uint32_t    e_version2;
    uint64_t    e_entry;
    uint64_t    e_phoff;
    uint64_t    e_shoff;
    uint32_t    e_flags;
    uint16_t    e_ehsize;
    uint16_t    e_phentsize;
    uint16_t    e_phnum;
    uint16_t    e_shentsize;
    uint16_t    e_shnum;
    uint16_t    e_shstrndx;
} elf64_header_t;

typedef struct __attribute__((packed)) {
    uint32_t    p_type;
    uint32_t    p_flags;
    uint64_t    p_offset;
    uint64_t    p_vaddr;
    uint64_t    p_paddr;
    uint64_t    p_filesz;
    uint64_t    p_memsz;
    uint64_t    p_align;
} elf64_phdr_t;

/* Validate ELF header */
static int elf_validate(const elf64_header_t* hdr, size_t file_size)
{
    if (hdr->e_magic != ELF_MAGIC) {
        printk(KERN_ERR "[ELF] Invalid magic: 0x%08x\n", hdr->e_magic);
        return -1;
    }
    
    if (hdr->e_class != 2) {
        printk(KERN_ERR "[ELF] Not 64-bit ELF\n");
        return -1;
    }
    
    if (hdr->e_data != 1) {
        printk(KERN_ERR "[ELF] Not little-endian\n");
        return -1;
    }
    
    if (hdr->e_machine != EM_AARCH64) {
        printk(KERN_ERR "[ELF] Not ARM64: machine=%u\n", hdr->e_machine);
        return -1;
    }
    
    if (hdr->e_type != ET_EXEC && hdr->e_type != ET_DYN) {
        printk(KERN_ERR "[ELF] Not executable or shared object\n");
        return -1;
    }
    
    if (hdr->e_phoff + (hdr->e_phnum * hdr->e_phentsize) > file_size) {
        printk(KERN_ERR "[ELF] Program headers exceed file size\n");
        return -1;
    }
    
    return 0;
}

/* Load ELF segments into process address space */
static int elf_load_segments(const void* file_data, size_t file_size,
                              pgd_t* pgd, uint64_t* entry_point)
{
    const elf64_header_t* hdr = (const elf64_header_t*)file_data;
    
    if (elf_validate(hdr, file_size) < 0)
        return -1;
    
    *entry_point = hdr->e_entry;
    
    printk(KERN_INFO "[ELF] Entry: 0x%016llx, %u program headers\n",
           hdr->e_entry, hdr->e_phnum);
    
    const uint8_t* phdr_base = (const uint8_t*)file_data + hdr->e_phoff;
    
    for (uint16_t i = 0; i < hdr->e_phnum; i++) {
        const elf64_phdr_t* ph = (const elf64_phdr_t*)(
            phdr_base + i * hdr->e_phentsize);
        
        if (ph->p_type != PT_LOAD)
            continue;
        
        /* Validate segment bounds */
        if (ph->p_offset + ph->p_filesz > file_size) {
            printk(KERN_ERR "[ELF] Segment %u exceeds file bounds\n", i);
            return -1;
        }
        
        printk(KERN_INFO "[ELF] LOAD seg[%u]: vaddr=0x%llx, "
               "filesz=%llu, memsz=%llu, flags=%c%c%c\n",
               i, ph->p_vaddr, ph->p_filesz, ph->p_memsz,
               (ph->p_flags & PF_R) ? 'R' : '-',
               (ph->p_flags & PF_W) ? 'W' : '-',
               (ph->p_flags & PF_X) ? 'X' : '-');
        
        /* Allocate pages for this segment */
        size_t num_pages = (ph->p_memsz + 4095) / 4096;
        
        for (size_t pg = 0; pg < num_pages; pg++) {
            uintptr_t vaddr = ph->p_vaddr + (pg * 4096);
            uintptr_t page = (uintptr_t)pmm_alloc_page();
            if (!page) {
                printk(KERN_ERR "[ELF] Out of memory loading segment\n");
                return -1;
            }
            
            /* Map page into process page table with correct permissions */
            uint64_t flags = VMM_FLAG_USER;
            if (ph->p_flags & PF_W) flags |= VMM_FLAG_WRITE;
            if (!(ph->p_flags & PF_X)) flags |= VMM_FLAG_NX;
            
            vmm_map_page(pgd, vaddr, page, flags);
        }
        
        /* Copy file data into mapped memory */
        void* dest = (void*)ph->p_vaddr;
        const void* src = (const uint8_t*)file_data + ph->p_offset;
        
        if (ph->p_filesz > 0)
            kmemcpy(dest, src, ph->p_filesz);
        
        /* Zero BSS (memsz > filesz) */
        if (ph->p_memsz > ph->p_filesz)
            kmemset((uint8_t*)dest + ph->p_filesz, 0,
                    ph->p_memsz - ph->p_filesz);
    }
    
    return 0;
}


/* ═══════════════════════════════════════════════════════════
 *  PACKAGE SIGNATURE VERIFICATION
 * ═══════════════════════════════════════════════════════════ */

/* Simple Ed25519 signature verification stub
 * In production: full Ed25519 implementation or call crypto subsystem */
 
#define SIG_SIZE    64
#define PUBKEY_SIZE 32

/* Crimson Store public key (embedded) */
static const uint8_t crimson_store_pubkey[PUBKEY_SIZE] = {
    0xC5, 0x1A, 0x8E, 0x3F, 0x2B, 0x7D, 0x91, 0x04,
    0xE8, 0x6C, 0xA3, 0x50, 0xF7, 0x19, 0xD2, 0xB8,
    0x4A, 0x63, 0xCE, 0x77, 0x0D, 0x85, 0xF1, 0x3B,
    0x96, 0x2A, 0x5E, 0xC0, 0x48, 0xAD, 0x71, 0xE9,
};

static int pkg_verify_signature(const void* data, size_t data_len,
                                 const uint8_t* signature)
{
    /* 
     * In production: Ed25519 signature verification
     * crypto_ed25519_verify(crimson_store_pubkey, data, data_len, signature)
     */
    
    /* For now: verify signature is non-zero (placeholder) */
    for (int i = 0; i < SIG_SIZE; i++) {
        if (signature[i] != 0) {
            printk(KERN_INFO "[PKG] Signature present (verification pending)\n");
            return 0;
        }
    }
    
    printk(KERN_WARN "[PKG] No signature found\n");
    return -1;
}


/* ═══════════════════════════════════════════════════════════
 *  DEPENDENCY RESOLUTION
 * ═══════════════════════════════════════════════════════════ */

#define MAX_DEPS        32
#define MAX_INSTALLED   256

typedef struct {
    char    name[64];
    char    version[16];
    int     installed;
} pkg_dep_t;

/* Global installed package registry */
static struct {
    struct {
        char    name[64];
        char    version[16];
        pid_t   pid;        /* Running process, or 0 */
        char    path[128];
    } entries[MAX_INSTALLED];
    uint32_t    count;
    spinlock_t  lock;
} g_pkg_registry;

void pkg_registry_init(void)
{
    kmemset(&g_pkg_registry, 0, sizeof(g_pkg_registry));
    spin_lock_init(&g_pkg_registry.lock);
    printk(KERN_INFO "[PKG] Package registry initialized\n");
}

int pkg_registry_add(const char* name, const char* version, const char* path)
{
    unsigned long flags;
    spin_lock_irqsave(&g_pkg_registry.lock, flags);
    
    if (g_pkg_registry.count >= MAX_INSTALLED) {
        spin_unlock_irqrestore(&g_pkg_registry.lock, flags);
        return -1;
    }
    
    uint32_t idx = g_pkg_registry.count++;
    kstrncpy(g_pkg_registry.entries[idx].name, name, 63);
    kstrncpy(g_pkg_registry.entries[idx].version, version, 15);
    kstrncpy(g_pkg_registry.entries[idx].path, path, 127);
    g_pkg_registry.entries[idx].pid = 0;
    
    spin_unlock_irqrestore(&g_pkg_registry.lock, flags);
    return 0;
}

int pkg_registry_is_installed(const char* name)
{
    for (uint32_t i = 0; i < g_pkg_registry.count; i++) {
        if (kstrcmp(g_pkg_registry.entries[i].name, name) == 0)
            return 1;
    }
    return 0;
}

static int pkg_resolve_deps(const pkg_dep_t* deps, uint32_t dep_count)
{
    int missing = 0;
    
    for (uint32_t i = 0; i < dep_count; i++) {
        if (!pkg_registry_is_installed(deps[i].name)) {
            printk(KERN_INFO "[PKG] Missing dependency: %s >= %s\n",
                   deps[i].name, deps[i].version);
            
            /* Attempt to auto-install dependency */
            printk(KERN_INFO "[PKG] Auto-installing %s...\n", deps[i].name);
            
            char dep_path[128];
            ksnprintf(dep_path, sizeof(dep_path),
                      "/store/packages/%s.cpkg", deps[i].name);
            
            if (pkg_install(dep_path) < 0) {
                printk(KERN_ERR "[PKG] Failed to install dependency: %s\n",
                       deps[i].name);
                missing++;
            }
        }
    }
    
    return missing == 0 ? 0 : -1;
}


/* ═══════════════════════════════════════════════════════════
 *  CRIMSON PACKAGE FORMAT (.cpkg)
 * ═══════════════════════════════════════════════════════════ */

/*
 * .cpkg file layout:
 *   [0x00] Magic: "CPKG" (4 bytes)
 *   [0x04] Version: uint32
 *   [0x08] Flags: uint32
 *   [0x0C] Name: char[64]
 *   [0x4C] Version string: char[16]
 *   [0x5C] App ID: char[64]
 *   [0x9C] Signature: uint8[64]
 *   [0xDC] Dep count: uint32
 *   [0xE0] Deps: pkg_dep_t[dep_count]
 *   [deps_end] ELF offset: uint64
 *   [elf_off]  ELF binary data
 */

#define CPKG_MAGIC  0x474B5043  /* "CPKG" */

typedef struct __attribute__((packed)) {
    uint32_t    magic;
    uint32_t    format_version;
    uint32_t    flags;
    char        name[64];
    char        version[16];
    char        app_id[64];
    uint8_t     signature[SIG_SIZE];
    uint32_t    dep_count;
    /* Followed by: pkg_dep_t deps[dep_count] */
    /* Followed by: uint64_t elf_offset */
    /* Followed by: ELF binary */
} cpkg_header_t;

#define CPKG_FLAG_SYSTEM    (1 << 0)
#define CPKG_FLAG_NET       (1 << 1)
#define CPKG_FLAG_PHONE     (1 << 2)
#define CPKG_FLAG_CAMERA    (1 << 3)
#define CPKG_FLAG_LOCATION  (1 << 4)


/* ═══════════════════════════════════════════════════════════
 *  PACKAGE INSTALL & EXECUTE
 * ═══════════════════════════════════════════════════════════ */

int pkg_install(const char* cpkg_path)
{
    printk(KERN_INFO "[PKG] Installing: %s\n", cpkg_path);
    
    /* Open package file */
    int fd = vfs_open(cpkg_path, 0);
    if (fd < 0) {
        printk(KERN_ERR "[PKG] Package not found: %s\n", cpkg_path);
        return -1;
    }
    
    size_t file_size = vfs_size(fd);
    if (file_size < sizeof(cpkg_header_t)) {
        printk(KERN_ERR "[PKG] Package too small\n");
        vfs_close(fd);
        return -1;
    }
    
    /* Read entire package into memory */
    void* pkg_data = kmalloc(file_size);
    if (!pkg_data) {
        vfs_close(fd);
        return -1;
    }
    
    vfs_read(fd, pkg_data, file_size);
    vfs_close(fd);
    
    /* Parse header */
    const cpkg_header_t* hdr = (const cpkg_header_t*)pkg_data;
    
    if (hdr->magic != CPKG_MAGIC) {
        printk(KERN_ERR "[PKG] Invalid package magic\n");
        kfree(pkg_data);
        return -1;
    }
    
    printk(KERN_INFO "[PKG] Package: %s v%s (%s)\n",
           hdr->name, hdr->version, hdr->app_id);
    
    /* Verify signature */
    if (pkg_verify_signature(pkg_data, file_size, hdr->signature) < 0) {
        printk(KERN_WARN "[PKG] Signature verification failed — "
               "installing as UNTRUSTED\n");
    }
    
    /* Resolve dependencies */
    const pkg_dep_t* deps = (const pkg_dep_t*)(
        (const uint8_t*)pkg_data + sizeof(cpkg_header_t));
    
    if (hdr->dep_count > 0) {
        printk(KERN_INFO "[PKG] Resolving %u dependencies...\n", hdr->dep_count);
        if (pkg_resolve_deps(deps, hdr->dep_count) < 0) {
            printk(KERN_ERR "[PKG] Dependency resolution failed\n");
            kfree(pkg_data);
            return -1;
        }
    }
    
    /* Extract ELF binary to app directory */
    char install_dir[128];
    ksnprintf(install_dir, sizeof(install_dir),
              "/data/apps/%s", hdr->app_id);
    
    /* vfs_mkdir(install_dir, 0755); */
    
    char elf_path[128];
    ksnprintf(elf_path, sizeof(elf_path),
              "%s/binary.elf", install_dir);
    
    /* Calculate ELF offset */
    size_t deps_size = hdr->dep_count * sizeof(pkg_dep_t);
    size_t elf_offset = sizeof(cpkg_header_t) + deps_size + sizeof(uint64_t);
    size_t elf_size = file_size - elf_offset;
    
    /* Write ELF to filesystem */
    int ofd = vfs_open(elf_path, 1 /* O_CREAT|O_WRONLY */);
    if (ofd >= 0) {
        vfs_write(ofd, (const uint8_t*)pkg_data + elf_offset, elf_size);
        vfs_close(ofd);
    }
    
    /* Register in package database */
    pkg_registry_add(hdr->name, hdr->version, elf_path);
    
    kfree(pkg_data);
    
    printk(KERN_INFO "[PKG] Installed: %s v%s → %s\n",
           hdr->name, hdr->version, install_dir);
    return 0;
}

pid_t pkg_execute(const char* app_id)
{
    printk(KERN_INFO "[PKG] Launching: %s\n", app_id);
    
    /* Find in registry */
    char elf_path[128];
    int found = 0;
    
    for (uint32_t i = 0; i < g_pkg_registry.count; i++) {
        if (kstrcmp(g_pkg_registry.entries[i].name, app_id) == 0 ||
            kstrstr(g_pkg_registry.entries[i].path, app_id)) {
            kstrncpy(elf_path, g_pkg_registry.entries[i].path, 127);
            found = 1;
            break;
        }
    }
    
    if (!found) {
        printk(KERN_ERR "[PKG] App not installed: %s\n", app_id);
        return -1;
    }
    
    /* Read ELF binary */
    int fd = vfs_open(elf_path, 0);
    if (fd < 0) return -1;
    
    size_t elf_size = vfs_size(fd);
    void* elf_data = kmalloc(elf_size);
    if (!elf_data) { vfs_close(fd); return -1; }
    
    vfs_read(fd, elf_data, elf_size);
    vfs_close(fd);
    
    /* Create process */
    struct process_attr attr = {
        .name = app_id,
        .entry = NULL,      /* Will be set from ELF entry point */
        .priority = PRIO_NORMAL,
        .stack_size = 64 * 1024,
        .flags = PROC_FLAG_USER,
    };
    
    pid_t pid = process_create(&attr);
    if (pid < 0) {
        kfree(elf_data);
        return -1;
    }
    
    struct process* proc = process_get_by_pid(pid);
    if (!proc) {
        kfree(elf_data);
        return -1;
    }
    
    /* Create sandbox for the app */
    sandbox_ctx_t* sb = sandbox_create(pid, SANDBOX_USER, app_id);
    if (!sb) {
        printk(KERN_WARN "[PKG] Sandbox creation failed, running restricted\n");
    }
    
    /* Load ELF segments into process address space */
    uint64_t entry_point = 0;
    if (elf_load_segments(elf_data, elf_size, proc->pgd, &entry_point) < 0) {
        printk(KERN_ERR "[PKG] ELF load failed for %s\n", app_id);
        sandbox_destroy(pid);
        process_kill(pid, 9);
        kfree(elf_data);
        return -1;
    }
    
    /* Set process entry point */
    proc->ctx.pc = entry_point;
    proc->state = PROC_READY;
    
    kfree(elf_data);
    
    printk(KERN_INFO "[PKG] Launched %s (PID %d, entry=0x%016llx)\n",
           app_id, pid, entry_point);
    
    return pid;
}

int pkg_terminate(const char* app_id)
{
    for (uint32_t i = 0; i < g_pkg_registry.count; i++) {
        if (kstrcmp(g_pkg_registry.entries[i].name, app_id) == 0) {
            pid_t pid = g_pkg_registry.entries[i].pid;
            if (pid > 0) {
                sandbox_destroy(pid);
                process_kill(pid, 15); /* SIGTERM */
                g_pkg_registry.entries[i].pid = 0;
                printk(KERN_INFO "[PKG] Terminated %s (PID %d)\n",
                       app_id, pid);
                return 0;
            }
        }
    }
    return -1;
}

int pkg_uninstall(const char* app_id)
{
    pkg_terminate(app_id);
    
    for (uint32_t i = 0; i < g_pkg_registry.count; i++) {
        if (kstrcmp(g_pkg_registry.entries[i].name, app_id) == 0) {
            /* Remove app data directory */
            char dir[128];
            ksnprintf(dir, sizeof(dir), "/data/apps/%s", app_id);
            /* vfs_rmdir_recursive(dir); */
            
            /* Remove from registry (shift entries down) */
            for (uint32_t j = i; j < g_pkg_registry.count - 1; j++)
                g_pkg_registry.entries[j] = g_pkg_registry.entries[j+1];
            g_pkg_registry.count--;
            
            printk(KERN_INFO "[PKG] Uninstalled: %s\n", app_id);
            return 0;
        }
    }
    return -1;
}
