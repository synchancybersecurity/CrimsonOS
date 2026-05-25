/* Copyright (c) 2026 SynChanCyberSecurity LLC. All Rights Reserved. */
/*
 * Crimson OS - Security Subsystem Header
 * MAC, Verified Boot, Sandboxing, Exploit Mitigations (ASLR/NX/CFI)
 */

#ifndef _CRIMSON_SECURITY_H
#define _CRIMSON_SECURITY_H

#include <crimson/types.h>
#include <crimson/process.h>
#include <crimson/spinlock.h>

#define SECURITY_VERSION        "1.0.0"

/* ============================================================
 *  MANDATORY ACCESS CONTROL (MAC)
 * ============================================================ */

/* Security contexts */
#define SEC_CTX_UNCLASSIFIED    0
#define SEC_CTX_PUBLIC          1
#define SEC_CTX_INTERNAL        2
#define SEC_CTX_CONFIDENTIAL    3
#define SEC_CTX_SECRET          4
#define SEC_CTX_TOP_SECRET      5
#define SEC_CTX_MAX             6

/* MAC domains */
#define SEC_DOMAIN_KERNEL       0   /* Kernel - full access */
#define SEC_DOMAIN_SYSTEM       1   /* System services */
#define SEC_DOMAIN_SERVICE      2   /* User services (network, etc) */
#define SEC_DOMAIN_APP          3   /* Normal applications */
#define SEC_DOMAIN_ISOLATED     4   /* Highly isolated (sandboxed) */
#define SEC_DOMAIN_PEN_TEST     5   /* Penetration testing tools */
#define SEC_DOMAIN_MAX          6

/* MAC permissions */
#define MAC_PERM_READ           0x01
#define MAC_PERM_WRITE          0x02
#define MAC_PERM_EXEC           0x04
#define MAC_PERM_APPEND         0x08
#define MAC_PERM_CREATE         0x10
#define MAC_PERM_DELETE         0x20
#define MAC_PERM_ADMIN          0x80

/* Types */
#define SEC_TYPE_NONE           0
#define SEC_TYPE_PROCESS        1
#define SEC_TYPE_FILE           2
#define SEC_TYPE_SOCKET         3
#define SEC_TYPE_SHM            4
#define SEC_TYPE_MSGQ           5
#define SEC_TYPE_DEVICE         6

/* Capability flags */
#define CAP_CHOWN               (1ULL << 0)
#define CAP_DAC_OVERRIDE        (1ULL << 1)
#define CAP_DAC_READ_SEARCH     (1ULL << 2)
#define CAP_FOWNER              (1ULL << 3)
#define CAP_FSETID              (1ULL << 4)
#define CAP_KILL                (1ULL << 5)
#define CAP_SETGID              (1ULL << 6)
#define CAP_SETUID              (1ULL << 7)
#define CAP_SETPCAP             (1ULL << 8)
#define CAP_NET_BIND_SERVICE    (1ULL << 9)
#define CAP_NET_BROADCAST       (1ULL << 10)
#define CAP_NET_ADMIN           (1ULL << 11)
#define CAP_NET_RAW             (1ULL << 12)
#define CAP_SYS_ADMIN           (1ULL << 13)
#define CAP_SYS_BOOT            (1ULL << 14)
#define CAP_SYS_CHROOT          (1ULL << 15)
#define CAP_SYS_MODULE          (1ULL << 16)
#define CAP_SYS_NICE            (1ULL << 17)
#define CAP_SYS_PACCT           (1ULL << 18)
#define CAP_SYS_PTRACE          (1ULL << 19)
#define CAP_SYS_RAWIO           (1ULL << 20)
#define CAP_SYS_RESOURCE        (1ULL << 21)
#define CAP_SYS_TIME            (1ULL << 22)
#define CAP_SYS_TTY_CONFIG      (1ULL << 23)
#define CAP_MKNOD               (1ULL << 24)
#define CAP_LEASE               (1ULL << 25)
#define CAP_AUDIT_CONTROL       (1ULL << 26)
#define CAP_AUDIT_WRITE         (1ULL << 27)
#define CAP_MAC_ADMIN           (1ULL << 28)
#define CAP_MAC_OVERRIDE        (1ULL << 29)
#define CAP_PEN_TEST            (1ULL << 30)   /* Penetration testing capability */
#define CAP_ALL                 0xFFFFFFFF

/* Default capability sets per domain */
#define CAPS_DOMAIN_KERNEL      CAP_ALL
#define CAPS_DOMAIN_SYSTEM      (CAP_NET_ADMIN | CAP_NET_RAW | CAP_SYS_ADMIN | CAP_SYS_BOOT | CAP_SYS_CHROOT | CAP_SYS_NICE | CAP_SYS_RESOURCE | CAP_AUDIT_CONTROL | CAP_AUDIT_WRITE)
#define CAPS_DOMAIN_SERVICE     (CAP_NET_BIND_SERVICE | CAP_NET_RAW | CAP_SYS_TIME | CAP_KILL)
#define CAPS_DOMAIN_APP         (CAP_NET_BIND_SERVICE)
#define CAPS_DOMAIN_ISOLATED    0
#define CAPS_DOMAIN_PEN_TEST    (CAP_NET_RAW | CAP_NET_ADMIN | CAP_SYS_PTRACE | CAP_SYS_RAWIO | CAP_PEN_TEST)

typedef struct mac_label {
    uint32_t    domain;
    uint32_t    context;
    uint32_t    type;
    uint64_t    caps;
    uint32_t    flags;
} mac_label_t;

/* MAC label flags */
#define MAC_FLAG_UNTRUSTED      0x01    /* Came from untrusted source */
#define MAC_FLAG_SANDBOXED      0x02    /* In sandbox */
#define MAC_FLAG_NO_EXEC        0x04    /* Cannot execute */
#define MAC_FLAG_NO_NETWORK     0x08    /* No network access */
#define MAC_FLAG_IMMUTABLE      0x10    /* Label cannot be changed */

/* MAC policies */
typedef struct mac_policy {
    uint32_t    subject_domain;
    uint32_t    object_domain;
    uint32_t    permissions;    /* Allowed perms (MAC_PERM_*) */
    uint32_t    flags;
} mac_policy_t;

/* ============================================================
 *  VERIFIED BOOT
 * ============================================================ */

#define VB_MAGIC                0x56424F4F  /* "VBOO" */
#define VB_VERSION              1
#define VB_KEY_SIZE             32
#define VB_HASH_SIZE            32
#define VB_MAX_ROLLBACK         32
#define VB_PARTITION_NAME_LEN   32
#define VB_MAX_PARTITIONS       16

/* Boot states */
#define BOOT_STATE_GREEN        0   /* Fully verified */
#define BOOT_STATE_YELLOW       1   /* Custom key, verified */
#define BOOT_STATE_ORANGE       2   /* No verification, self-signed */
#define BOOT_STATE_RED          3   /* Verification failed */
#define BOOT_STATE_EIO          4   /* I/O error during verification */

/* Verified boot header */
typedef struct vb_partition {
    char        name[VB_PARTITION_NAME_LEN];
    uint8_t     hash[VB_HASH_SIZE];
    uint64_t    size;
    uint32_t    rollback_index;
} vb_partition_t;

typedef struct vb_header {
    uint32_t        magic;
    uint32_t        version;
    uint32_t        algorithm;
    uint64_t        flags;
    uint32_t        partition_count;
    uint32_t        rollback_index_location;
    uint8_t         pubkey[VB_KEY_SIZE];
    uint8_t         signature[64];
    uint8_t         reserved[32];
    vb_partition_t  partitions[VB_MAX_PARTITIONS];
} vb_header_t;

/* ============================================================
 *  SANDBOX
 * ============================================================ */

#define SANDBOX_MAX             64
#define SB_NAME_LEN             64
#define SB_MAX_MOUNTS           16
#define SB_MAX_RULES            32

/* Sandbox resource limits */
#define SB_LIMIT_CPU_TIME       0
#define SB_LIMIT_MEMORY         1
#define SB_LIMIT_OPEN_FILES     2
#define SB_LIMIT_PROCESSES      3
#define SB_LIMIT_NET_BANDWIDTH  4
#define SB_LIMIT_SYSCALLS       5
#define SB_LIMIT_MAX            6

/* Sandbox rules */
#define SB_RULE_ALLOW           1
#define SB_RULE_DENY            2
#define SB_RULE_READ_ONLY       3
#define SB_RULE_LOG             4

#define SB_TARGET_FILE          1
#define SB_TARGET_DIR           2
#define SB_TARGET_NET           3
#define SB_TARGET_SYSCALL       4
#define SB_TARGET_CAPABILITY    5

typedef struct sandbox_rule {
    uint32_t    action;         /* SB_RULE_* */
    uint32_t    target_type;    /* SB_TARGET_* */
    char        target[256];    /* Path/syscall name */
    uint32_t    flags;
} sandbox_rule_t;

typedef struct sandbox_limits {
    uint64_t    cpu_time_ms;
    uint64_t    memory_max_bytes;
    uint32_t    max_open_files;
    uint32_t    max_processes;
    uint32_t    max_net_bandwidth_kbps;
    uint32_t    allowed_syscalls[16];  /* Bitmap */
} sandbox_limits_t;

typedef struct sandbox {
    uint32_t            id;
    char                name[SB_NAME_LEN];
    pid_t               owner;
    uint32_t            active;
    mac_label_t         label;
    sandbox_rule_t      rules[SB_MAX_RULES];
    uint32_t            rule_count;
    sandbox_limits_t    limits;
    uint32_t            namespace_pid;
    uint32_t            namespace_net;
    uint32_t            namespace_mount;
    spinlock_t          lock;
} sandbox_t;

/* ============================================================
 *  EXPLOIT MITIGATIONS
 * ============================================================ */

/* ASLR configuration */
#define ASLR_BITS               18      /* 256KB randomization */
#define ASLR_STACK_BITS         16
#define ASLR_MMAP_BITS          14
#define ASLR_PIE_BITS           18

typedef struct aslr_state {
    uint32_t    enabled;
    uint32_t    stack_randomization;
    uint32_t    mmap_randomization;
    uint32_t    pie_randomization;
    uint32_t    brute_force_detection;
    uint32_t    reexec_count;
} aslr_state_t;

/* CFI (Control Flow Integrity) */
#define CFI_MAX_SHADOW_ENTRIES  4096

typedef struct cfi_entry {
    uintptr_t   function_start;
    uintptr_t   function_end;
    uint32_t    valid_targets[8];  /* Bitmap of allowed call targets */
} cfi_entry_t;

typedef struct cfi_state {
    uint32_t        enabled;
    cfi_entry_t     shadow_stack[CFI_MAX_SHADOW_ENTRIES];
    uint32_t        shadow_count;
    uint32_t        violations;
    uint32_t        strict_mode;    /* Fatal on violation vs log */
} cfi_state_t;

/* NX (No-Execute) bit - managed via page tables */
#define NX_ENABLED              1

/* Stack canary */
#define STACK_CANARY_SIZE       8
#define STACK_CANARY_VALUE      0xDEADBEEFCAFEBAB0ULL

/* ============================================================
 *  AUDIT LOG
 * ============================================================ */

#define AUDIT_MAX_ENTRIES       4096
#define AUDIT_MSG_LEN           256

/* Audit types */
#define AUDIT_SYSCALL           1
#define AUDIT_MAC_VIOLATION     2
#define AUDIT_AUTH              3
#define AUDIT_FILE_ACCESS       4
#define AUDIT_NETWORK           5
#define AUDIT_PROCESS           6
#define AUDIT_SECURITY          7
#define AUDIT_CAP_CHANGE        8

typedef struct audit_entry {
    uint64_t    timestamp;
    uint32_t    type;
    pid_t       pid;
    uid_t       uid;
    uint32_t    result;
    char        message[AUDIT_MSG_LEN];
} audit_entry_t;

/* ============================================================
 *  SECURITY STATE
 * ============================================================ */

typedef struct security_state {
    /* MAC */
    mac_label_t     default_labels[SEC_DOMAIN_MAX];
    mac_policy_t    policies[128];
    uint32_t        policy_count;
    uint32_t        mac_enforcing;      /* 0 = permissive, 1 = enforcing */

    /* Verified boot */
    vb_header_t     vb_header;
    uint32_t        boot_state;
    uint32_t        rollback_index;

    /* Sandbox */
    sandbox_t       sandboxes[SANDBOX_MAX];
    uint32_t        sandbox_count;

    /* ASLR */
    aslr_state_t    aslr;

    /* CFI */
    cfi_state_t     cfi;

    /* NX */
    uint32_t        nx_enabled;

    /* Stack canaries */
    uint64_t        system_canary;

    /* Audit */
    audit_entry_t   audit_log[AUDIT_MAX_ENTRIES];
    uint32_t        audit_head;
    uint32_t        audit_count;
    spinlock_t      audit_lock;
    uint32_t        audit_enabled;

    /* Master lock */
    spinlock_t      lock;

    /* Security level */
    uint32_t        security_level;     /* 0-5 */
    uint32_t        selinux_compat;     /* SELinux policy compatibility */
    uint32_t        apparmor_compat;    /* AppArmor policy compatibility */
} security_state_t;

/* ============================================================
 *  GLOBAL
 * ============================================================ */

extern security_state_t g_security;

/* ============================================================
 *  FUNCTION DECLARATIONS
 * ============================================================ */

/* MAC */
void mac_init(void);
void mac_label_init(mac_label_t* label, uint32_t domain, uint32_t context);
int mac_check_access(mac_label_t* subject, mac_label_t* object, uint32_t perm);
int mac_check_capability(pid_t pid, uint64_t cap);
void mac_set_domain(pid_t pid, uint32_t domain);
void mac_set_context(pid_t pid, uint32_t context);
void mac_add_policy(uint32_t subject_domain, uint32_t object_domain, uint32_t perms);
void mac_load_default_policies(void);
void mac_set_enforcing(uint32_t enforcing);
void mac_print_labels(void);
const char* mac_domain_name(uint32_t domain);
const char* mac_context_name(uint32_t ctx);

/* Verified boot */
void vb_init(void);
int vb_verify_partition(const char* name, const uint8_t* data, size_t size);
int vb_verify_chain(void);
void vb_set_state(uint32_t state);
void vb_print_status(void);
const char* vb_state_name(uint32_t state);

/* Sandbox */
void sandbox_init(void);
int sandbox_create(const char* name, pid_t owner);
void sandbox_destroy(uint32_t sb_id);
int sandbox_add_rule(uint32_t sb_id, uint32_t action, uint32_t target_type, const char* target);
int sandbox_apply(uint32_t sb_id, pid_t pid);
int sandbox_enter(uint32_t sb_id);
void sandbox_set_limits(uint32_t sb_id, sandbox_limits_t* limits);
int sandbox_check_access(uint32_t sb_id, uint32_t target_type, const char* target, uint32_t access);
sandbox_t* sandbox_get(uint32_t id);
void sandbox_list(void);
void sandbox_print_rules(uint32_t sb_id);
const char* sandbox_target_name(uint32_t t);
const char* sandbox_action_name(uint32_t a);

/* ASLR */
void aslr_init(void);
uintptr_t aslr_randomize_stack(uintptr_t base);
uintptr_t aslr_randomize_mmap(uintptr_t base, size_t size);
uintptr_t aslr_randomize_pie(uintptr_t base);
void aslr_set_enabled(uint32_t enabled);
void aslr_print_status(void);

/* CFI */
void cfi_init(void);
void cfi_register_function(uintptr_t start, uintptr_t end);
int cfi_verify_target(uintptr_t caller, uintptr_t target);
void cfi_shadow_push(uintptr_t ret_addr);
uintptr_t cfi_shadow_pop(void);
void cfi_violation(uintptr_t expected, uintptr_t actual);
void cfi_set_strict(uint32_t strict);
void cfi_print_status(void);

/* NX */
void nx_init(void);
void nx_enable(void);
void nx_set_page_nx(uintptr_t vaddr);
void nx_set_page_exec(uintptr_t vaddr);
int nx_check_region(uintptr_t vaddr, size_t size);

/* Stack canary */
void canary_init(void);
uint64_t canary_generate(void);
void canary_set_thread(uint64_t canary);
int canary_verify(uint64_t canary);
void canary_print_status(void);

/* Audit */
void audit_init(void);
void audit_log(uint32_t type, pid_t pid, uid_t uid, uint32_t result, const char* fmt, ...);
void audit_print_recent(uint32_t count);
void audit_enable(uint32_t enable);
void audit_clear(void);
const char* audit_type_name(uint32_t type);

/* General */
void security_init(void);
void security_print_status(void);
void security_run_tests(void);

/* Security commands for shell */
void security_shell_help(void);
void security_shell_dispatch(const char* cmd, int argc, char** argv);

#endif /* _CRIMSON_SECURITY_H */
