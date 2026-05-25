/* Copyright (c) 2026 SynChanCyberSecurity LLC. All Rights Reserved. */
/*
 * Crimson OS - Package Manager Header
 * .crimson package format, Ed25519 signing, repositories
 */

#ifndef _CRIMSON_PKG_H
#define _CRIMSON_PKG_H

#include <crimson/types.h>

#define PKG_VERSION             "1.0.0"

/* Package file magic & format */
#define CRIMSON_PKG_MAGIC       0x43524D53  /* "CRMS" */
#define CRIMSON_PKG_VERSION     1
#define CRIMSON_PKG_EXT         ".crimson"

/* Signature types */
#define PKG_SIG_NONE            0
#define PKG_SIG_ED25519         1

/* Compression types */
#define PKG_COMP_NONE           0
#define PKG_COMP_LZ4            1
#define PKG_COMP_ZSTD           2

/* Package states */
#define PKG_STATE_AVAILABLE     0
#define PKG_STATE_INSTALLED     1
#define PKG_STATE_UPDATABLE     2
#define PKG_STATE_BROKEN        3
#define PKG_STATE_PURGED        4

/* Limits */
#define PKG_MAX_NAME            128
#define PKG_MAX_VERSION         32
#define PKG_MAX_DESC            1024
#define PKG_MAX_AUTHOR          256
#define PKG_MAX_DEPS            64
#define PKG_MAX_FILES           4096
#define PKG_MAX_REPOS           16
#define PKG_MAX_PACKAGES        65536
#define PKG_HASH_LEN            32
#define PKG_SIG_LEN             64
#define PKG_PUBKEY_LEN          32

/* Permissions */
#define PKG_PERM_NETWORK        0x0001
#define PKG_PERM_STORAGE        0x0002
#define PKG_PERM_CAMERA         0x0004
#define PKG_PERM_MIC            0x0008
#define PKG_PERM_LOCATION       0x0010
#define PKG_PERM_CONTACTS       0x0020
#define PKG_PERM_SMS            0x0040
#define PKG_PERM_PHONE          0x0080
#define PKG_PERM_SENSORS        0x0100
#define PKG_PERM_BLUETOOTH      0x0200
#define PKG_PERM_USB            0x0400
#define PKG_PERM_ADMIN          0x0800
#define PKG_PERM_PEN_TEST       0x1000  /* Kali-style penetration testing tools */

typedef struct pkg_file_entry {
    char        path[256];
    uint32_t    offset;
    uint32_t    size;
    uint32_t    compressed_size;
    uint8_t     hash[PKG_HASH_LEN];
    uint32_t    perms;
    uint32_t    uid;
    uint32_t    gid;
} pkg_file_entry_t;

typedef struct pkg_dependency {
    char        name[PKG_MAX_NAME];
    char        version_min[PKG_MAX_VERSION];
    char        version_max[PKG_MAX_VERSION];
    uint32_t    optional;
} pkg_dependency_t;

typedef struct pkg_header {
    uint32_t    magic;
    uint32_t    format_version;
    uint32_t    pkg_version_major;
    uint32_t    pkg_version_minor;
    uint32_t    pkg_version_patch;
    char        name[PKG_MAX_NAME];
    char        version[PKG_MAX_VERSION];
    char        description[PKG_MAX_DESC];
    char        author[PKG_MAX_AUTHOR];
    char        license[64];
    char        homepage[256];
    char        category[64];
    uint32_t    target_arch;        /* ARCH_ARM64, etc */
    uint32_t    sdk_version;
    uint32_t    installed_size;
    uint32_t    download_size;
    uint32_t    compression;
    uint32_t    file_count;
    uint32_t    dep_count;
    uint32_t    permissions;
    uint32_t    flags;
    uint8_t     signature[PKG_SIG_LEN];
    uint8_t     signer_pubkey[PKG_PUBKEY_LEN];
    uint32_t    sig_type;
    uint8_t     pkg_hash[PKG_HASH_LEN];
    uint64_t    timestamp;
} pkg_header_t;

typedef struct pkg_entry {
    pkg_header_t    header;
    uint32_t        state;
    uint32_t        repo_id;
    char            install_path[256];
    uint64_t        install_time;
    uint32_t        auto_update;
    struct pkg_entry* next;
} pkg_entry_t;

typedef struct pkg_repo {
    uint32_t    id;
    uint32_t    enabled;
    char        name[64];
    char        url[256];
    char        pubkey[PKG_PUBKEY_LEN];
    uint32_t    priority;
    uint64_t    last_sync;
    uint32_t    package_count;
    uint32_t    trusted;        /* 1 = official, 0 = community */
} pkg_repo_t;

typedef struct pkg_state {
    pkg_entry_t*    installed;
    pkg_repo_t      repos[PKG_MAX_REPOS];
    uint32_t        repo_count;
    uint32_t        total_installed;
    char            install_root[256];
    char            db_path[256];
    char            cache_path[256];
    uint8_t         system_pubkey[PKG_PUBKEY_LEN];
    uint32_t        auto_update;
} pkg_state_t;

/* Lifecycle */
void pkg_init(void);
void pkg_shutdown(void);
pkg_state_t* pkg_get_state(void);

/* Package file I/O */
int pkg_read_header(const char* path, pkg_header_t* hdr);
int pkg_verify_signature(const char* path, pkg_header_t* hdr);
int pkg_verify_hash(const char* path, pkg_header_t* hdr);
int pkg_extract(const char* pkg_path, const char* dest, pkg_header_t* hdr);

/* Ed25519 crypto */
int pkg_ed25519_verify(const uint8_t* msg, size_t msg_len, const uint8_t* sig, const uint8_t* pubkey);
void pkg_hash_sha256(const uint8_t* data, size_t len, uint8_t* out);

/* Database */
int pkg_db_load(void);
int pkg_db_save(void);
pkg_entry_t* pkg_db_find(const char* name);
pkg_entry_t* pkg_db_find_installed(const char* name);
void pkg_db_add(pkg_entry_t* entry);
void pkg_db_remove(const char* name);

/* Repository management */
int pkg_repo_add(const char* name, const char* url, const uint8_t* pubkey, uint32_t trusted);
void pkg_repo_remove(uint32_t repo_id);
void pkg_repo_enable(uint32_t repo_id, uint32_t enable);
int pkg_repo_sync(uint32_t repo_id);
void pkg_repo_sync_all(void);
pkg_repo_t* pkg_repo_get(uint32_t id);
void pkg_repo_list(void);

/* Package operations */
int pkg_install(const char* pkg_path);
int pkg_install_from_repo(const char* name);
int pkg_remove(const char* name, uint32_t purge);
int pkg_upgrade(const char* name);
int pkg_upgrade_all(void);
int pkg_purge(const char* name);

/* Dependency resolution */
int pkg_resolve_deps(pkg_header_t* hdr, char** missing, uint32_t* missing_count);
int pkg_check_deps(const char* name);
int pkg_install_deps(pkg_header_t* hdr);

/* Query & search */
void pkg_list_installed(void);
void pkg_list_available(uint32_t repo_id);
void pkg_search(const char* query);
void pkg_info(const char* name);
void pkg_info_header(pkg_header_t* hdr);
int pkg_is_installed(const char* name);
int pkg_has_update(const char* name);

/* File listing & verification */
void pkg_list_files(const char* name);
int pkg_verify_files(const char* name);
int pkg_verify_all(void);

/* CLI commands */
void pkg_cmd_install(int argc, char** argv);
void pkg_cmd_remove(int argc, char** argv);
void pkg_cmd_purge(int argc, char** argv);
void pkg_cmd_upgrade(int argc, char** argv);
void pkg_cmd_search(int argc, char** argv);
void pkg_cmd_info(int argc, char** argv);
void pkg_cmd_list(int argc, char** argv);
void pkg_cmd_verify(int argc, char** argv);
void pkg_cmd_repo(int argc, char** argv);
void pkg_cmd_sync(int argc, char** argv);
void pkg_cmd_clean(int argc, char** argv);

/* Shell integration */
void pkg_shell_help(void);
void pkg_shell_dispatch(const char* cmd, int argc, char** argv);

/* Utility */
int pkg_parse_version(const char* ver, uint32_t* major, uint32_t* minor, uint32_t* patch);
int pkg_version_compare(const char* a, const char* b);
void pkg_format_size(uint32_t bytes, char* out, size_t out_len);
const char* pkg_perm_name(uint32_t perm);

#endif /* _CRIMSON_PKG_H */
