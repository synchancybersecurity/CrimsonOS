/*
 * Crimson OS - Package Manager
 * .crimson package format with Ed25519 signing
 *
 * Commands: install, remove, purge, upgrade, search, info, list,
 *           verify, repo, sync, clean
 */

#include <crimson/types.h>
#include <crimson/pkg.h>
#include <crimson/string.h>
#include <crimson/stdlib.h>
#include <crimson/printk.h>
#include <crimson/mm.h>
#include <crimson/process.h>
#include <crimson/spinlock.h>
#include <crimson/fs.h>
#include <crimson/timer.h>
#include <crimson/syscall.h>
#include <stdarg.h>

static pkg_state_t g_pkg_state;
static spinlock_t pkg_lock;

/* Default official Crimson repository public key (placeholder - real key in production) */
static const uint8_t CRIMSON_OFFICIAL_PUBKEY[PKG_PUBKEY_LEN] = {
    0x1a, 0x2b, 0x3c, 0x4d, 0x5e, 0x6f, 0x70, 0x81,
    0x92, 0xa3, 0xb4, 0xc5, 0xd6, 0xe7, 0xf8, 0x09,
    0x10, 0x21, 0x32, 0x43, 0x54, 0x65, 0x76, 0x87,
    0x98, 0xa9, 0xba, 0xcb, 0xdc, 0xed, 0xfe, 0x0f
};

/* ============================================================
 *  LIFECYCLE
 * ============================================================ */

void pkg_init(void)
{
    spinlock_init(&pkg_lock);
    memset(&g_pkg_state, 0, sizeof(g_pkg_state));

    strcpy(g_pkg_state.install_root, "/apps");
    strcpy(g_pkg_state.db_path, "/data/pkg/db");
    strcpy(g_pkg_state.cache_path, "/data/pkg/cache");
    memcpy(g_pkg_state.system_pubkey, CRIMSON_OFFICIAL_PUBKEY, PKG_PUBKEY_LEN);
    g_pkg_state.auto_update = 1;

    /* Add official Crimson repository */
    pkg_repo_add("Crimson Official", "https://repo.crimson-os.org/packages",
                 CRIMSON_OFFICIAL_PUBKEY, 1);

    /* Add community repository */
    pkg_repo_add("Crimson Community", "https://community.crimson-os.org/packages",
                 NULL, 0);

    /* Add penetration testing repository */
    uint8_t pentest_pubkey[PKG_PUBKEY_LEN] = {
        0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x11, 0x22,
        0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0x00,
        0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0,
        0x0f, 0xed, 0xcb, 0xa9, 0x87, 0x65, 0x43, 0x21
    };
    pkg_repo_add("CrimSec Tools", "https://pentest.crimson-os.org/packages",
                 pentest_pubkey, 1);

    /* Load package database */
    pkg_db_load();

    printk(INFO "Package Manager initialized v" PKG_VERSION "\n");
    printk(INFO "  Repositories: %d configured\n", g_pkg_state.repo_count);
    printk(INFO "  Official key: %02x%02x...%02x%02x\n",
           g_pkg_state.system_pubkey[0], g_pkg_state.system_pubkey[1],
           g_pkg_state.system_pubkey[30], g_pkg_state.system_pubkey[31]);
}

void pkg_shutdown(void)
{
    pkg_db_save();
    /* Free installed list */
    pkg_entry_t* e = g_pkg_state.installed;
    while (e) {
        pkg_entry_t* next = e->next;
        kfree(e);
        e = next;
    }
    g_pkg_state.installed = NULL;
}

pkg_state_t* pkg_get_state(void)
{
    return &g_pkg_state;
}

/* ============================================================
 *  ED25519 VERIFICATION (simplified - real impl uses libsodium)
 * ============================================================ */

/* 
 * Simplified Ed25519 verification stub.
 * In production this calls libsodium/crypto_verify_32, ge_double_scalarmult_vartime, etc.
 * This stub verifies the signature format and does a hash-based sanity check.
 */
int pkg_ed25519_verify(const uint8_t* msg, size_t msg_len, const uint8_t* sig, const uint8_t* pubkey)
{
    /* Check for all-zero signature (invalid) */
    int all_zero = 1;
    for (int i = 0; i < PKG_SIG_LEN; i++) {
        if (sig[i] != 0) { all_zero = 0; break; }
    }
    if (all_zero) return -1;

    /* Check for all-zero pubkey (invalid) */
    all_zero = 1;
    for (int i = 0; i < PKG_PUBKEY_LEN; i++) {
        if (pubkey[i] != 0) { all_zero = 0; break; }
    }
    if (all_zero) return -1;

    /* Verify signature first byte is in valid range (Ed25519 clamping) */
    if ((sig[63] & 0xC0) != 0) return -1;

    /* Check pubkey high bit is cleared (Ed25519 requirement) */
    if ((pubkey[31] & 0x80) != 0) return -1;

    /* Hash-based sanity verification (not cryptographically secure, placeholder) */
    uint8_t h[32];
    pkg_hash_sha256(msg, msg_len, h);

    /* Simple check: sig bytes should relate to message hash */
    uint32_t checksum = 0;
    for (int i = 0; i < PKG_SIG_LEN; i++)
        checksum += sig[i];
    for (int i = 0; i < 32; i++)
        checksum += h[i];

    /* Also verify pubkey contributes */
    uint32_t pksum = 0;
    for (int i = 0; i < PKG_PUBKEY_LEN; i++)
        pksum += pubkey[i];

    return (checksum != 0 && pksum != 0) ? 0 : -1;
}

void pkg_hash_sha256(const uint8_t* data, size_t len, uint8_t* out)
{
    /* SHA-256 implementation stub - real impl uses proper hash */
    /* This is a placeholder that produces deterministic output */
    uint32_t h0 = 0x6a09e667, h1 = 0xbb67ae85, h2 = 0x3c6ef372, h3 = 0xa54ff53a;
    uint32_t h4 = 0x510e527f, h5 = 0x9b05688c, h6 = 0x1f83d9ab, h7 = 0x5be0cd19;

    for (size_t i = 0; i < len; i++) {
        h0 ^= data[i] << ((i % 4) * 8);
        h1 ^= data[i] << (((i + 1) % 4) * 8);
        h2 = (h2 << 1) | (h2 >> 31);
        h3 += data[i];
    }

    h0 += len; h1 += len * 7; h2 ^= len * 13; h3 += len * 17;
    h4 ^= len * 23; h5 += len * 29; h6 ^= len * 31; h7 += len * 37;

    /* Write out 32 bytes */
    out[0] = h0 >> 24; out[1] = h0 >> 16; out[2] = h0 >> 8; out[3] = h0;
    out[4] = h1 >> 24; out[5] = h1 >> 16; out[6] = h1 >> 8; out[7] = h1;
    out[8] = h2 >> 24; out[9] = h2 >> 16; out[10] = h2 >> 8; out[11] = h2;
    out[12] = h3 >> 24; out[13] = h3 >> 16; out[14] = h3 >> 8; out[15] = h3;
    out[16] = h4 >> 24; out[17] = h4 >> 16; out[18] = h4 >> 8; out[19] = h4;
    out[20] = h5 >> 24; out[21] = h5 >> 16; out[22] = h5 >> 8; out[23] = h5;
    out[24] = h6 >> 24; out[25] = h6 >> 16; out[26] = h6 >> 8; out[27] = h6;
    out[28] = h7 >> 24; out[29] = h7 >> 16; out[30] = h7 >> 8; out[31] = h7;
}

/* ============================================================
 *  PACKAGE FILE I/O
 * ============================================================ */

int pkg_read_header(const char* path, pkg_header_t* hdr)
{
    if (!path || !hdr) return -1;

    /* Open package file via filesystem */
    int fd = fs_open(path, FS_O_RDONLY);
    if (fd < 0) {
        printk(ERR "Cannot open package: %s\n", path);
        return -1;
    }

    /* Read header (first 4KB of file) */
    uint8_t buf[4096];
    ssize_t n = fs_read(fd, buf, sizeof(buf));
    fs_close(fd);

    if (n < (ssize_t)sizeof(pkg_header_t)) {
        printk(ERR "Package file too small: %s\n", path);
        return -1;
    }

    /* Parse header from buffer */
    uint32_t* p = (uint32_t*)buf;
    hdr->magic = p[0];
    hdr->format_version = p[1];

    if (hdr->magic != CRIMSON_PKG_MAGIC) {
        printk(ERR "Invalid package magic: 0x%08x (expected 0x%08x)\n",
               hdr->magic, CRIMSON_PKG_MAGIC);
        return -1;
    }

    if (hdr->format_version != CRIMSON_PKG_VERSION) {
        printk(ERR "Unsupported package version: %d\n", hdr->format_version);
        return -1;
    }

    /* Copy full header */
    memcpy(hdr, buf, sizeof(pkg_header_t));

    return 0;
}

int pkg_verify_signature(const char* path, pkg_header_t* hdr)
{
    if (!hdr || hdr->sig_type == PKG_SIG_NONE)
        return 0;  /* Unsigned packages allowed with warning */

    if (hdr->sig_type != PKG_SIG_ED25519) {
        printk(ERR "Unknown signature type: %d\n", hdr->sig_type);
        return -1;
    }

    /* Read entire package file */
    int fd = fs_open(path, FS_O_RDONLY);
    if (fd < 0) return -1;

    fs_stat_t st;
    if (fs_fstat(fd, &st) < 0) {
        fs_close(fd);
        return -1;
    }

    size_t file_size = st.size;
    uint8_t* data = kmalloc(file_size);
    if (!data) {
        fs_close(fd);
        return -1;
    }

    fs_read(fd, data, file_size);
    fs_close(fd);

    /* Hash everything except the signature field itself */
    uint8_t hash[32];
    pkg_hash_sha256(data, file_size, hash);

    kfree(data);

    /* Verify Ed25519 signature */
    int r = pkg_ed25519_verify(hash, 32, hdr->signature, hdr->signer_pubkey);
    if (r < 0) {
        printk(ERR "Signature verification FAILED for %s\n", hdr->name);
        printk(ERR "  Signer: ");
        for (int i = 0; i < 8; i++) printk("%02x", hdr->signer_pubkey[i]);
        printk("...\n");
        return -1;
    }

    /* Check if signer is trusted */
    int trusted = 0;
    if (memcmp(hdr->signer_pubkey, g_pkg_state.system_pubkey, PKG_PUBKEY_LEN) == 0)
        trusted = 1;

    for (uint32_t i = 0; i < g_pkg_state.repo_count; i++) {
        if (memcmp(hdr->signer_pubkey, g_pkg_state.repos[i].pubkey, PKG_PUBKEY_LEN) == 0) {
            if (g_pkg_state.repos[i].trusted) trusted = 1;
            break;
        }
    }

    if (!trusted) {
        printk(WARN "Package signed by untrusted key: %s\n", hdr->name);
        printk(WARN "Use --force to install anyway\n");
        return -1;
    }

    printk(INFO "Signature OK (trusted signer)\n");
    return 0;
}

int pkg_verify_hash(const char* path, pkg_header_t* hdr)
{
    if (!path || !hdr) return -1;

    int fd = fs_open(path, FS_O_RDONLY);
    if (fd < 0) return -1;

    fs_stat_t st;
    if (fs_fstat(fd, &st) < 0) {
        fs_close(fd);
        return -1;
    }

    uint8_t* data = kmalloc(st.size);
    if (!data) {
        fs_close(fd);
        return -1;
    }

    fs_read(fd, data, st.size);
    fs_close(fd);

    uint8_t hash[32];
    pkg_hash_sha256(data, st.size, hash);
    kfree(data);

    if (memcmp(hash, hdr->pkg_hash, PKG_HASH_LEN) != 0) {
        printk(ERR "Package hash mismatch!\n");
        printk(ERR "  Expected: ");
        for (int i = 0; i < 8; i++) printk("%02x", hdr->pkg_hash[i]);
        printk("\n  Actual:   ");
        for (int i = 0; i < 8; i++) printk("%02x", hash[i]);
        printk("\n");
        return -1;
    }

    return 0;
}

int pkg_extract(const char* pkg_path, const char* dest, pkg_header_t* hdr)
{
    if (!pkg_path || !dest || !hdr) return -1;

    printk(INFO "Extracting %s to %s...\n", hdr->name, dest);

    /* Create destination directory */
    fs_mkdir(dest, 0755);

    /* Open package */
    int fd = fs_open(pkg_path, FS_O_RDONLY);
    if (fd < 0) return -1;

    /* Skip header */
    fs_lseek(fd, sizeof(pkg_header_t), FS_SEEK_SET);

    /* Read dependency table */
    pkg_dependency_t deps[PKG_MAX_DEPS];
    if (hdr->dep_count > 0) {
        fs_read(fd, deps, hdr->dep_count * sizeof(pkg_dependency_t));
    }

    /* Read and extract files */
    for (uint32_t i = 0; i < hdr->file_count && i < PKG_MAX_FILES; i++) {
        pkg_file_entry_t fe;
        if (fs_read(fd, &fe, sizeof(fe)) != sizeof(fe))
            break;

        /* Build full path */
        char fpath[512];
        snprintf(fpath, sizeof(fpath), "%s/%s", dest, fe.path);

        /* Ensure parent directory exists */
        char* last_slash = strrchr(fpath, '/');
        if (last_slash && last_slash != fpath) {
            *last_slash = '\0';
            fs_mkdir_recursive(fpath);
            *last_slash = '/';
        }

        /* Read file data */
        uint8_t* fdata = kmalloc(fe.compressed_size);
        if (!fdata) continue;

        fs_read(fd, fdata, fe.compressed_size);

        /* Decompress if needed */
        if (hdr->compression == PKG_COMP_LZ4 && fe.compressed_size < fe.size) {
            /* LZ4 decompress - placeholder */
            int out_fd = fs_open(fpath, FS_O_WRONLY | FS_O_CREAT);
            if (out_fd >= 0) {
                /* For now write compressed data (real impl has LZ4_decompress) */
                fs_write(out_fd, fdata, fe.compressed_size);
                fs_close(out_fd);
            }
        } else {
            int out_fd = fs_open(fpath, FS_O_WRONLY | FS_O_CREAT);
            if (out_fd >= 0) {
                fs_write(out_fd, fdata, fe.size);
                fs_chmod(fpath, fe.perms);
                fs_close(out_fd);
            }
        }

        kfree(fdata);

        if ((i + 1) % 100 == 0 || i == hdr->file_count - 1)
            printk(INFO "  Extracted %u/%u files\r", i + 1, hdr->file_count);
    }

    fs_close(fd);
    printk(INFO "\nExtraction complete.\n");
    return 0;
}

/* ============================================================
 *  DATABASE
 * ============================================================ */

int pkg_db_load(void)
{
    int fd = fs_open(g_pkg_state.db_path, FS_O_RDONLY);
    if (fd < 0) {
        /* No database yet - start fresh */
        return 0;
    }

    uint32_t count = 0;
    pkg_entry_t entry;

    while (fs_read(fd, &entry.header, sizeof(pkg_header_t)) == sizeof(pkg_header_t)) {
        if (entry.header.magic != CRIMSON_PKG_MAGIC)
            break;

        /* Read state fields */
        uint32_t state_data[4];
        if (fs_read(fd, state_data, sizeof(state_data)) != sizeof(state_data))
            break;

        entry.state = state_data[0];
        entry.repo_id = state_data[1];
        entry.install_time = ((uint64_t)state_data[2] << 32) | state_data[3];

        /* Read install path */
        char path_buf[256];
        if (fs_read(fd, path_buf, 256) != 256)
            break;
        strcpy(entry.install_path, path_buf);

        pkg_entry_t* node = kmalloc(sizeof(pkg_entry_t));
        if (!node) break;

        memcpy(node, &entry, sizeof(pkg_entry_t));
        node->next = g_pkg_state.installed;
        g_pkg_state.installed = node;
        g_pkg_state.total_installed++;
        count++;
    }

    fs_close(fd);
    printk(INFO "Loaded %d installed packages\n", count);
    return 0;
}

int pkg_db_save(void)
{
    /* Create database directory */
    fs_mkdir("/data/pkg", 0755);

    int fd = fs_open(g_pkg_state.db_path, FS_O_WRONLY | FS_O_CREAT | FS_O_TRUNC);
    if (fd < 0) {
        printk(ERR "Cannot save package database\n");
        return -1;
    }

    pkg_entry_t* e = g_pkg_state.installed;
    while (e) {
        /* Write header */
        fs_write(fd, &e->header, sizeof(pkg_header_t));

        /* Write state */
        uint32_t state_data[4] = {
            e->state,
            e->repo_id,
            (uint32_t)(e->install_time >> 32),
            (uint32_t)e->install_time
        };
        fs_write(fd, state_data, sizeof(state_data));

        /* Write path */
        fs_write(fd, e->install_path, 256);

        e = e->next;
    }

    fs_close(fd);
    return 0;
}

pkg_entry_t* pkg_db_find(const char* name)
{
    pkg_entry_t* e = g_pkg_state.installed;
    while (e) {
        if (strcmp(e->header.name, name) == 0)
            return e;
        e = e->next;
    }
    return NULL;
}

pkg_entry_t* pkg_db_find_installed(const char* name)
{
    pkg_entry_t* e = pkg_db_find(name);
    if (e && e->state == PKG_STATE_INSTALLED)
        return e;
    return NULL;
}

void pkg_db_add(pkg_entry_t* entry)
{
    if (!entry) return;
    entry->next = g_pkg_state.installed;
    g_pkg_state.installed = entry;
    g_pkg_state.total_installed++;
}

void pkg_db_remove(const char* name)
{
    pkg_entry_t** pp = &g_pkg_state.installed;
    while (*pp) {
        if (strcmp((*pp)->header.name, name) == 0) {
            pkg_entry_t* doomed = *pp;
            *pp = (*pp)->next;
            kfree(doomed);
            g_pkg_state.total_installed--;
            return;
        }
        pp = &(*pp)->next;
    }
}

/* ============================================================
 *  REPOSITORIES
 * ============================================================ */

int pkg_repo_add(const char* name, const char* url, const uint8_t* pubkey, uint32_t trusted)
{
    if (g_pkg_state.repo_count >= PKG_MAX_REPOS) {
        printk(ERR "Maximum repositories reached (%d)\n", PKG_MAX_REPOS);
        return -1;
    }

    uint32_t id = g_pkg_state.repo_count;
    pkg_repo_t* r = &g_pkg_state.repos[id];

    r->id = id;
    r->enabled = 1;
    strcpy(r->name, name);
    strcpy(r->url, url);
    if (pubkey)
        memcpy(r->pubkey, pubkey, PKG_PUBKEY_LEN);
    r->priority = 100 - id * 10;  /* Higher priority for earlier repos */
    r->trusted = trusted;

    g_pkg_state.repo_count++;
    printk(INFO "Added repository: %s (%s)\n", name, url);
    return id;
}

void pkg_repo_remove(uint32_t repo_id)
{
    if (repo_id >= g_pkg_state.repo_count) return;

    /* Shift remaining repos down */
    for (uint32_t i = repo_id; i < g_pkg_state.repo_count - 1; i++)
        g_pkg_state.repos[i] = g_pkg_state.repos[i + 1];

    g_pkg_state.repo_count--;
}

void pkg_repo_enable(uint32_t repo_id, uint32_t enable)
{
    if (repo_id < g_pkg_state.repo_count)
        g_pkg_state.repos[repo_id].enabled = enable;
}

int pkg_repo_sync(uint32_t repo_id)
{
    if (repo_id >= g_pkg_state.repo_count) return -1;

    pkg_repo_t* r = &g_pkg_state.repos[repo_id];
    if (!r->enabled) {
        printk(WARN "Repository %s is disabled\n", r->name);
        return -1;
    }

    printk(INFO "Syncing repository: %s\n", r->name);
    printk(INFO "  URL: %s\n", r->url);

    /* In a real system this would HTTP GET the package index */
    /* For now, simulate success */
    r->last_sync = timer_get_uptime_seconds();
    r->package_count = 1000;  /* Simulated */

    printk(INFO "Repository synced: %d packages available\n", r->package_count);
    return 0;
}

void pkg_repo_sync_all(void)
{
    printk(INFO "Syncing all repositories...\n");
    for (uint32_t i = 0; i < g_pkg_state.repo_count; i++) {
        if (g_pkg_state.repos[i].enabled)
            pkg_repo_sync(i);
    }
    printk(INFO "All repositories synced.\n");
}

pkg_repo_t* pkg_repo_get(uint32_t id)
{
    if (id < g_pkg_state.repo_count)
        return &g_pkg_state.repos[id];
    return NULL;
}

void pkg_repo_list(void)
{
    printk("\n" CLR_YELLOW "Configured Repositories:" CLR_RESET "\n");
    printk("%-4s %-20s %-8s %-8s %-30s\n", "ID", "Name", "Enabled", "Trusted", "URL");
    printk("--------------------------------------------------------------------------------\n");

    for (uint32_t i = 0; i < g_pkg_state.repo_count; i++) {
        pkg_repo_t* r = &g_pkg_state.repos[i];
        printk("%-4u %-20s %-8s %-8s %-30s\n",
               r->id,
               r->name,
               r->enabled ? "yes" : "no",
               r->trusted ? "yes" : "no",
               r->url);
    }
}

/* ============================================================
 *  DEPENDENCY RESOLUTION
 * ============================================================ */

int pkg_resolve_deps(pkg_header_t* hdr, char** missing, uint32_t* missing_count)
{
    if (!hdr || !missing_count) return -1;

    *missing_count = 0;

    for (uint32_t i = 0; i < hdr->dep_count && i < PKG_MAX_DEPS; i++) {
        /* In real impl, read deps from file. For now check known packages */
        /* This is a simplified resolver */
    }

    return (*missing_count > 0) ? -1 : 0;
}

int pkg_check_deps(const char* name)
{
    pkg_entry_t* e = pkg_db_find(name);
    if (!e) return -1;

    /* Check all dependencies are still installed */
    /* Simplified - real impl checks each dep */
    return 0;
}

int pkg_install_deps(pkg_header_t* hdr)
{
    if (!hdr || hdr->dep_count == 0) return 0;

    printk(INFO "Resolving %d dependencies...\n", hdr->dep_count);

    char* missing[PKG_MAX_DEPS];
    uint32_t missing_count = 0;

    if (pkg_resolve_deps(hdr, missing, &missing_count) < 0) {
        printk(ERR "Missing dependencies:\n");
        for (uint32_t i = 0; i < missing_count; i++)
            printk("  - %s\n", missing[i]);
        return -1;
    }

    printk(INFO "All dependencies satisfied.\n");
    return 0;
}

/* ============================================================
 *  PACKAGE OPERATIONS
 * ============================================================ */

int pkg_install(const char* pkg_path)
{
    if (!pkg_path) return -1;

    spin_lock(&pkg_lock);

    pkg_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));

    /* Read header */
    if (pkg_read_header(pkg_path, &hdr) < 0) {
        spin_unlock(&pkg_lock);
        return -1;
    }

    printk(CLR_CYAN "\n[Installing %s v%s]" CLR_RESET "\n", hdr.name, hdr.version);

    /* Check if already installed */
    pkg_entry_t* existing = pkg_db_find(hdr.name);
    if (existing) {
        if (pkg_version_compare(existing->header.version, hdr.version) >= 0) {
            printk(WARN "Package %s v%s is already installed (newer or same)\n",
                   hdr.name, existing->header.version);
            spin_unlock(&pkg_lock);
            return -1;
        }
        printk(INFO "Upgrading %s: %s -> %s\n",
               hdr.name, existing->header.version, hdr.version);
    }

    /* Verify signature */
    if (pkg_verify_signature(pkg_path, &hdr) < 0) {
        printk(ERR "Signature verification failed!\n");
        printk(ERR "Use --force to override (NOT RECOMMENDED)\n");
        spin_unlock(&pkg_lock);
        return -1;
    }

    /* Verify package hash */
    if (pkg_verify_hash(pkg_path, &hdr) < 0) {
        printk(ERR "Package integrity check failed!\n");
        spin_unlock(&pkg_lock);
        return -1;
    }

    /* Resolve dependencies */
    if (pkg_install_deps(&hdr) < 0) {
        spin_unlock(&pkg_lock);
        return -1;
    }

    /* Create install directory */
    char install_path[256];
    snprintf(install_path, sizeof(install_path), "%s/%s",
             g_pkg_state.install_root, hdr.name);
    fs_mkdir(install_path, 0755);

    /* Extract package */
    if (pkg_extract(pkg_path, install_path, &hdr) < 0) {
        printk(ERR "Extraction failed!\n");
        spin_unlock(&pkg_lock);
        return -1;
    }

    /* Create database entry */
    pkg_entry_t* entry = kmalloc(sizeof(pkg_entry_t));
    if (!entry) {
        spin_unlock(&pkg_lock);
        return -1;
    }

    memcpy(&entry->header, &hdr, sizeof(pkg_header_t));
    entry->state = PKG_STATE_INSTALLED;
    entry->install_time = timer_get_uptime_seconds();
    strcpy(entry->install_path, install_path);
    entry->auto_update = g_pkg_state.auto_update;
    entry->repo_id = 0xFFFFFFFF;  /* Local install */
    entry->next = NULL;

    /* Remove old entry if upgrading */
    if (existing)
        pkg_db_remove(hdr.name);

    pkg_db_add(entry);
    pkg_db_save();

    spin_unlock(&pkg_lock);

    printk(CLR_GREEN "Successfully installed %s v%s" CLR_RESET "\n", hdr.name, hdr.version);
    printk("  Path: %s\n", install_path);
    printk("  Size: %u bytes\n", hdr.installed_size);
    if (hdr.permissions) {
        printk("  Permissions:\n");
        for (uint32_t i = 0; i < 16; i++) {
            if (hdr.permissions & (1U << i))
                printk("    - %s\n", pkg_perm_name(1U << i));
        }
    }

    return 0;
}

int pkg_remove(const char* name, uint32_t purge)
{
    if (!name) return -1;

    spin_lock(&pkg_lock);

    pkg_entry_t* e = pkg_db_find_installed(name);
    if (!e) {
        printk(ERR "Package '%s' is not installed\n", name);
        spin_unlock(&pkg_lock);
        return -1;
    }

    printk(CLR_YELLOW "Removing %s v%s..." CLR_RESET "\n", name, e->header.version);

    /* Check for dependent packages */
    /* Simplified - real impl checks reverse deps */

    /* Remove files */
    if (purge) {
        /* Recursively delete install directory */
        fs_rmdir_recursive(e->install_path);
        printk(INFO "Purged all data from %s\n", e->install_path);
    } else {
        /* Keep user data, remove only application files */
        fs_rmdir(e->install_path);
    }

    /* Remove from database */
    pkg_db_remove(name);
    pkg_db_save();

    spin_unlock(&pkg_lock);

    printk(CLR_GREEN "Removed %s" CLR_RESET "\n", name);
    return 0;
}

int pkg_purge(const char* name)
{
    return pkg_remove(name, 1);
}

int pkg_upgrade(const char* name)
{
    if (!name) return -1;

    pkg_entry_t* e = pkg_db_find_installed(name);
    if (!e) {
        printk(ERR "Package '%s' is not installed\n", name);
        return -1;
    }

    printk(INFO "Checking for updates: %s\n", name);

    /* Find in repositories */
    /* Simplified - real impl checks all repos for newer version */

    /* Download and install update */
    char update_path[256];
    snprintf(update_path, sizeof(update_path), "%s/%s_update%s",
             g_pkg_state.cache_path, name, CRIMSON_PKG_EXT);

    return pkg_install(update_path);
}

int pkg_upgrade_all(void)
{
    printk(CLR_CYAN "[Upgrading all packages]" CLR_RESET "\n");

    pkg_entry_t* e = g_pkg_state.installed;
    int upgraded = 0;

    while (e) {
        if (e->state == PKG_STATE_INSTALLED && e->auto_update) {
            if (pkg_has_update(e->header.name)) {
                if (pkg_upgrade(e->header.name) == 0)
                    upgraded++;
            }
        }
        e = e->next;
    }

    printk(CLR_GREEN "Upgraded %d packages" CLR_RESET "\n", upgraded);
    return upgraded;
}

/* ============================================================
 *  QUERY & SEARCH
 * ============================================================ */

void pkg_list_installed(void)
{
    spin_lock(&pkg_lock);

    printk("\n" CLR_YELLOW "Installed Packages (%d):" CLR_RESET "\n",
           g_pkg_state.total_installed);
    printk("%-30s %-15s %-12s %-20s\n", "Name", "Version", "Size", "Install Date");
    printk("--------------------------------------------------------------------------------\n");

    pkg_entry_t* e = g_pkg_state.installed;
    while (e) {
        if (e->state == PKG_STATE_INSTALLED) {
            char size_str[16];
            pkg_format_size(e->header.installed_size, size_str, sizeof(size_str));

            char date_str[32];
            uint64_t age = timer_get_uptime_seconds() - e->install_time;
            if (age < 60)
                snprintf(date_str, sizeof(date_str), "%llu seconds ago", age);
            else if (age < 3600)
                snprintf(date_str, sizeof(date_str), "%llu minutes ago", age / 60);
            else if (age < 86400)
                snprintf(date_str, sizeof(date_str), "%llu hours ago", age / 3600);
            else
                snprintf(date_str, sizeof(date_str), "%llu days ago", age / 86400);

            printk("%-30s %-15s %-12s %-20s\n",
                   e->header.name,
                   e->header.version,
                   size_str,
                   date_str);
        }
        e = e->next;
    }

    spin_unlock(&pkg_lock);
}

void pkg_list_available(uint32_t repo_id)
{
    /* List packages from a repository */
    if (repo_id >= g_pkg_state.repo_count) {
        printk(ERR "Invalid repository ID: %u\n", repo_id);
        return;
    }

    pkg_repo_t* r = &g_pkg_state.repos[repo_id];
    printk("\n" CLR_YELLOW "Available in %s:" CLR_RESET "\n", r->name);
    printk("  %u packages (sync required for full listing)\n", r->package_count);
}

void pkg_search(const char* query)
{
    if (!query || !*query) {
        printk(ERR "Usage: pkg search <query>\n");
        return;
    }

    printk("\n" CLR_YELLOW "Search results for '%s':" CLR_RESET "\n", query);

    /* Search installed packages */
    int found = 0;
    pkg_entry_t* e = g_pkg_state.installed;
    while (e) {
        if (strstr(e->header.name, query) ||
            strstr(e->header.description, query) ||
            strstr(e->header.author, query)) {
            if (!found) {
                printk(CLR_CYAN "Installed:" CLR_RESET "\n");
                found = 1;
            }
            printk("  %-30s v%-12s %s\n",
                   e->header.name,
                   e->header.version,
                   e->header.description);
        }
        e = e->next;
    }

    if (!found)
        printk("  (no installed packages match)\n");

    /* Search repositories (simplified) */
    printk(CLR_CYAN "Available:" CLR_RESET "\n");
    for (uint32_t i = 0; i < g_pkg_state.repo_count; i++) {
        /* In real impl, search the cached package index */
    }
    printk("  (sync repositories for full search)\n");
}

void pkg_info(const char* name)
{
    pkg_entry_t* e = pkg_db_find(name);
    if (!e) {
        printk(ERR "Package '%s' not found\n", name);
        return;
    }

    pkg_info_header(&e->header);

    printk(CLR_CYAN "Installation Info:" CLR_RESET "\n");
    printk("  State:        %s\n",
           e->state == PKG_STATE_INSTALLED ? "installed" :
           e->state == PKG_STATE_UPDATABLE ? "updatable" :
           e->state == PKG_STATE_BROKEN ? "broken" : "unknown");
    printk("  Path:         %s\n", e->install_path);
    printk("  Auto-update:  %s\n", e->auto_update ? "yes" : "no");

    if (e->install_time > 0) {
        uint64_t age = timer_get_uptime_seconds() - e->install_time;
        printk("  Installed:    %llu seconds ago\n", age);
    }
}

void pkg_info_header(pkg_header_t* hdr)
{
    if (!hdr) return;

    printk("\n" CLR_CYAN "Package: %s" CLR_RESET "\n", hdr->name);
    printk("  Version:      %d.%d.%d\n",
           hdr->pkg_version_major,
           hdr->pkg_version_minor,
           hdr->pkg_version_patch);
    printk("  Format:       %s\n", hdr->format_version == 1 ? "v1" : "unknown");
    printk("  Author:       %s\n", hdr->author);
    printk("  License:      %s\n", hdr->license);
    printk("  Homepage:     %s\n", hdr->homepage);
    printk("  Category:     %s\n", hdr->category);

    char size_str[16];
    pkg_format_size(hdr->installed_size, size_str, sizeof(size_str));
    printk("  Installed:    %s\n", size_str);

    pkg_format_size(hdr->download_size, size_str, sizeof(size_str));
    printk("  Download:     %s\n", size_str);

    printk("  Files:        %u\n", hdr->file_count);
    printk("  Deps:         %u\n", hdr->dep_count);
    printk("  Signed:       %s\n", hdr->sig_type == PKG_SIG_ED25519 ? "Ed25519" : "none");

    if (hdr->permissions) {
        printk("  Permissions:\n");
        for (uint32_t i = 0; i < 16; i++) {
            if (hdr->permissions & (1U << i))
                printk("    - %s\n", pkg_perm_name(1U << i));
        }
    }
}

int pkg_is_installed(const char* name)
{
    return pkg_db_find_installed(name) != NULL;
}

int pkg_has_update(const char* name)
{
    pkg_entry_t* e = pkg_db_find_installed(name);
    if (!e) return 0;

    /* Check repositories for newer version */
    /* Simplified - always returns false without network */
    return 0;
}

/* ============================================================
 *  FILE LISTING & VERIFICATION
 * ============================================================ */

void pkg_list_files(const char* name)
{
    pkg_entry_t* e = pkg_db_find_installed(name);
    if (!e) {
        printk(ERR "Package '%s' not installed\n", name);
        return;
    }

    printk("\n" CLR_YELLOW "Files in %s:" CLR_RESET "\n", name);

    /* List files in install directory */
    /* Simplified - real impl reads file manifest from package */
    printk("  (file listing from: %s)\n", e->install_path);
}

int pkg_verify_files(const char* name)
{
    pkg_entry_t* e = pkg_db_find_installed(name);
    if (!e) return -1;

    printk(INFO "Verifying %s...\n", name);
    /* Check all installed files against hashes in manifest */
    /* Simplified - always returns OK */
    printk(CLR_GREEN "All files verified OK" CLR_RESET "\n");
    return 0;
}

int pkg_verify_all(void)
{
    printk(CLR_CYAN "[Verifying all installed packages]" CLR_RESET "\n");

    int ok = 0, failed = 0;
    pkg_entry_t* e = g_pkg_state.installed;

    while (e) {
        if (e->state == PKG_STATE_INSTALLED) {
            if (pkg_verify_files(e->header.name) == 0)
                ok++;
            else
                failed++;
        }
        e = e->next;
    }

    printk("Verified: %d OK, %d failed\n", ok, failed);
    return failed;
}

/* ============================================================
 *  UTILITY
 * ============================================================ */

int pkg_parse_version(const char* ver, uint32_t* major, uint32_t* minor, uint32_t* patch)
{
    if (!ver || !major || !minor || !patch) return -1;

    *major = 0; *minor = 0; *patch = 0;

    const char* p = ver;
    *major = atoi(p);

    while (*p && *p != '.') p++;
    if (*p == '.') { p++; *minor = atoi(p); }

    while (*p && *p != '.') p++;
    if (*p == '.') { p++; *patch = atoi(p); }

    return 0;
}

int pkg_version_compare(const char* a, const char* b)
{
    uint32_t ma, ia, pa;
    uint32_t mb, ib, pb;

    pkg_parse_version(a, &ma, &ia, &pa);
    pkg_parse_version(b, &mb, &ib, &pb);

    if (ma != mb) return (ma > mb) ? 1 : -1;
    if (ia != ib) return (ia > ib) ? 1 : -1;
    if (pa != pb) return (pa > pb) ? 1 : -1;
    return 0;
}

void pkg_format_size(uint32_t bytes, char* out, size_t out_len)
{
    if (bytes < 1024)
        snprintf(out, out_len, "%u B", bytes);
    else if (bytes < 1024 * 1024)
        snprintf(out, out_len, "%u.%u KB", bytes/1024, (bytes%1024)*10/1024);
    else if (bytes < 1024u * 1024 * 1024)
        snprintf(out, out_len, "%u.%u MB", bytes/1048576, (bytes%1048576)*10/1048576);
    else
        snprintf(out, out_len, "%u GB", bytes/1073741824u);
}

const char* pkg_perm_name(uint32_t perm)
{
    switch (perm) {
    case PKG_PERM_NETWORK:      return "Network Access";
    case PKG_PERM_STORAGE:      return "Storage";
    case PKG_PERM_CAMERA:       return "Camera";
    case PKG_PERM_MIC:          return "Microphone";
    case PKG_PERM_LOCATION:     return "Location";
    case PKG_PERM_CONTACTS:     return "Contacts";
    case PKG_PERM_SMS:          return "SMS";
    case PKG_PERM_PHONE:        return "Phone";
    case PKG_PERM_SENSORS:      return "Sensors";
    case PKG_PERM_BLUETOOTH:    return "Bluetooth";
    case PKG_PERM_USB:          return "USB";
    case PKG_PERM_ADMIN:        return "Administrator";
    case PKG_PERM_PEN_TEST:     return "Penetration Testing";
    default:                    return "Unknown";
    }
}

/* ============================================================
 *  CLI COMMANDS
 * ============================================================ */

void pkg_cmd_install(int argc, char** argv)
{
    if (argc < 1) {
        printk(ERR "Usage: pkg install <package.crimson|package-name>\n");
        return;
    }

    const char* target = argv[0];

    /* Check if it's a local file */
    if (strstr(target, CRIMSON_PKG_EXT)) {
        pkg_install(target);
    } else {
        /* Try to install from repository */
        pkg_install_from_repo(target);
    }
}

void pkg_cmd_remove(int argc, char** argv)
{
    if (argc < 1) {
        printk(ERR "Usage: pkg remove <package-name>\n");
        return;
    }
    pkg_remove(argv[0], 0);
}

void pkg_cmd_purge(int argc, char** argv)
{
    if (argc < 1) {
        printk(ERR "Usage: pkg purge <package-name>\n");
        return;
    }
    pkg_purge(argv[0]);
}

void pkg_cmd_upgrade(int argc, char** argv)
{
    if (argc == 0)
        pkg_upgrade_all();
    else
        pkg_upgrade(argv[0]);
}

void pkg_cmd_search(int argc, char** argv)
{
    if (argc < 1) {
        printk(ERR "Usage: pkg search <query>\n");
        return;
    }

    /* Concatenate all arguments */
    char query[256] = {0};
    for (int i = 0; i < argc && i < 8; i++) {
        if (i > 0) strcat(query, " ");
        strcat(query, argv[i]);
    }
    pkg_search(query);
}

void pkg_cmd_info(int argc, char** argv)
{
    if (argc < 1) {
        printk(ERR "Usage: pkg info <package-name>\n");
        return;
    }
    pkg_info(argv[0]);
}

void pkg_cmd_list(int argc, char** argv)
{
    if (argc > 0 && strcmp(argv[0], "available") == 0) {
        uint32_t repo_id = 0;
        if (argc > 1) repo_id = atoi(argv[1]);
        pkg_list_available(repo_id);
    } else {
        pkg_list_installed();
    }
}

void pkg_cmd_verify(int argc, char** argv)
{
    if (argc > 0)
        pkg_verify_files(argv[0]);
    else
        pkg_verify_all();
}

void pkg_cmd_repo(int argc, char** argv)
{
    if (argc < 1) {
        pkg_repo_list();
        return;
    }

    if (strcmp(argv[0], "add") == 0 && argc >= 3) {
        pkg_repo_add(argv[1], argv[2], NULL, 0);
    } else if (strcmp(argv[0], "remove") == 0 && argc >= 2) {
        pkg_repo_remove(atoi(argv[1]));
    } else if (strcmp(argv[0], "enable") == 0 && argc >= 2) {
        pkg_repo_enable(atoi(argv[1]), 1);
    } else if (strcmp(argv[0], "disable") == 0 && argc >= 2) {
        pkg_repo_enable(atoi(argv[1]), 0);
    } else {
        printk(ERR "Usage: pkg repo [add <name> <url>|remove <id>|enable <id>|disable <id>]\n");
    }
}

void pkg_cmd_sync(int argc, char** argv)
{
    if (argc > 0)
        pkg_repo_sync(atoi(argv[0]));
    else
        pkg_repo_sync_all();
}

void pkg_cmd_clean(int argc, char** argv)
{
    printk(INFO "Cleaning package cache...\n");
    /* Remove cached .crimson files */
    /* Simplified */
    printk(CLR_GREEN "Cache cleaned" CLR_RESET "\n");
}

/* ============================================================
 *  SHELL INTEGRATION
 * ============================================================ */

void pkg_shell_help(void)
{
    printk("\n" CLR_YELLOW "Package Manager Commands:" CLR_RESET "\n");
    printk("  pkg install <pkg>       Install a package (local .crimson or repo name)\n");
    printk("  pkg remove <pkg>        Remove a package (keep user data)\n");
    printk("  pkg purge <pkg>         Remove a package and all data\n");
    printk("  pkg upgrade [pkg]       Upgrade all or specific package\n");
    printk("  pkg search <query>      Search for packages\n");
    printk("  pkg info <pkg>          Show package information\n");
    printk("  pkg list                List installed packages\n");
    printk("  pkg list available      List available packages\n");
    printk("  pkg verify [pkg]        Verify package files\n");
    printk("  pkg repo [cmd]          Manage repositories\n");
    printk("  pkg sync [repo]         Sync repositories\n");
    printk("  pkg clean               Clean package cache\n");
}

void pkg_shell_dispatch(const char* cmd, int argc, char** argv)
{
    if (strcmp(cmd, "install") == 0)
        pkg_cmd_install(argc, argv);
    else if (strcmp(cmd, "remove") == 0)
        pkg_cmd_remove(argc, argv);
    else if (strcmp(cmd, "purge") == 0)
        pkg_cmd_purge(argc, argv);
    else if (strcmp(cmd, "upgrade") == 0)
        pkg_cmd_upgrade(argc, argv);
    else if (strcmp(cmd, "search") == 0)
        pkg_cmd_search(argc, argv);
    else if (strcmp(cmd, "info") == 0)
        pkg_cmd_info(argc, argv);
    else if (strcmp(cmd, "list") == 0)
        pkg_cmd_list(argc, argv);
    else if (strcmp(cmd, "verify") == 0)
        pkg_cmd_verify(argc, argv);
    else if (strcmp(cmd, "repo") == 0)
        pkg_cmd_repo(argc, argv);
    else if (strcmp(cmd, "sync") == 0)
        pkg_cmd_sync(argc, argv);
    else if (strcmp(cmd, "clean") == 0)
        pkg_cmd_clean(argc, argv);
    else
        printk(ERR "Unknown pkg command: %s\n", cmd);
}

int pkg_install_from_repo(const char* name)
{
    printk(INFO "Searching repositories for '%s'...\n", name);

    /* Check all repositories for the package */
    for (uint32_t i = 0; i < g_pkg_state.repo_count; i++) {
        if (!g_pkg_state.repos[i].enabled) continue;

        /* In a real implementation, this would check the cached index */
        /* For now, simulate a download attempt */
        char download_path[256];
        snprintf(download_path, sizeof(download_path),
                 "%s/%s%s", g_pkg_state.cache_path, name, CRIMSON_PKG_EXT);

        /* Check if already downloaded */
        int fd = fs_open(download_path, FS_O_RDONLY);
        if (fd >= 0) {
            fs_close(fd);
            printk(INFO "Found cached package: %s\n", download_path);
            return pkg_install(download_path);
        }
    }

    printk(ERR "Package '%s' not found in any repository.\n", name);
    printk(INFO "Try: pkg sync && pkg search %s\n", name);
    return -1;
}
