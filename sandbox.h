/*
 * Crimson OS - App Sandbox & IPC System
 * Capability-based isolation with message-passing IPC
 */

#ifndef _CRIMSON_SANDBOX_H
#define _CRIMSON_SANDBOX_H

#include <crimson/types.h>
#include <crimson/process.h>

/* ── Capability Bits ── */
#define CAP_ADMIN       (1ULL << 0)
#define CAP_NET_RAW     (1ULL << 1)
#define CAP_NET_ADMIN   (1ULL << 2)
#define CAP_STORAGE     (1ULL << 3)
#define CAP_DISPLAY     (1ULL << 4)
#define CAP_AUDIO       (1ULL << 5)
#define CAP_CAMERA      (1ULL << 6)
#define CAP_GPS         (1ULL << 7)
#define CAP_BLUETOOTH   (1ULL << 8)
#define CAP_NFC         (1ULL << 9)
#define CAP_CELLULAR    (1ULL << 10)
#define CAP_SENSORS     (1ULL << 11)
#define CAP_PHONE       (1ULL << 12)
#define CAP_CONTACTS    (1ULL << 13)
#define CAP_SMS         (1ULL << 14)
#define CAP_MICROPHONE  (1ULL << 15)
#define CAP_BACKGROUND  (1ULL << 16)
#define CAP_BOOT        (1ULL << 17)
#define CAP_IPC         (1ULL << 18)
#define CAP_CRYPTO      (1ULL << 19)

/* Default cap sets */
#define CAPS_SYSTEM     (0xFFFFFFFFFFFFFFFFULL)
#define CAPS_USER_APP   (CAP_DISPLAY | CAP_STORAGE | CAP_IPC)
#define CAPS_NET_APP    (CAPS_USER_APP | CAP_NET_RAW)
#define CAPS_PHONE_APP  (CAPS_USER_APP | CAP_PHONE | CAP_CONTACTS | CAP_SMS | CAP_AUDIO | CAP_MICROPHONE)

/* ── Sandbox Policy ── */
#define SANDBOX_MAX_MEMORY      (256 * 1024 * 1024)  /* 256 MB per app */
#define SANDBOX_MAX_FILES       128
#define SANDBOX_MAX_THREADS     16
#define SANDBOX_MAX_IPC_PORTS   32

typedef enum {
    SANDBOX_NONE = 0,     /* Kernel/system — no restriction */
    SANDBOX_SYSTEM,       /* System services — limited caps */
    SANDBOX_USER,         /* User apps — strict isolation */
    SANDBOX_UNTRUSTED,    /* Downloaded/unknown — maximum restriction */
} sandbox_level_t;

typedef struct {
    sandbox_level_t  level;
    uint64_t         cap_granted;    /* Capabilities granted */
    uint64_t         cap_used;       /* Capabilities actually used (audit) */
    uint64_t         cap_denied;     /* Denied attempts (audit) */
    uintptr_t        mem_base;       /* Isolated memory region base */
    size_t           mem_size;       /* Allocated memory */
    size_t           mem_limit;      /* Maximum allowed */
    uint32_t         file_count;     /* Open file handles */
    uint32_t         thread_count;   /* Active threads */
    char             app_id[64];     /* Package identifier */
    char             data_dir[128];  /* App private storage path */
    uint32_t         violations;     /* Security violation count */
} sandbox_ctx_t;

/* ── IPC Message ── */
#define IPC_MSG_MAX_SIZE    4096
#define IPC_PORT_MAX        256
#define IPC_QUEUE_DEPTH     64

typedef enum {
    IPC_MSG_DATA = 0,
    IPC_MSG_REQUEST,
    IPC_MSG_REPLY,
    IPC_MSG_SIGNAL,
    IPC_MSG_SHARED_MEM,
} ipc_msg_type_t;

typedef struct {
    uint32_t         id;
    ipc_msg_type_t   type;
    pid_t            sender;
    pid_t            receiver;
    uint32_t         port;
    uint32_t         size;
    uint64_t         timestamp;
    uint8_t          data[IPC_MSG_MAX_SIZE];
} ipc_message_t;

typedef struct {
    uint32_t         port_id;
    pid_t            owner;
    char             name[32];
    uint64_t         required_caps;   /* Sender must have these caps */
    ipc_message_t    queue[IPC_QUEUE_DEPTH];
    uint32_t         head;
    uint32_t         tail;
    uint32_t         count;
    spinlock_t       lock;
    wait_queue_t     waiters;
} ipc_port_t;

/* ── Shared Memory Region ── */
#define SHM_MAX_REGIONS   64

typedef struct {
    uint32_t    id;
    uintptr_t   phys_addr;
    size_t      size;
    pid_t       owner;
    pid_t       clients[8];
    uint32_t    client_count;
    uint32_t    flags;        /* read-only, read-write */
} shm_region_t;

/* ── Sandbox API ── */
void            sandbox_init(void);
sandbox_ctx_t*  sandbox_create(pid_t pid, sandbox_level_t level, const char* app_id);
void            sandbox_destroy(pid_t pid);
int             sandbox_check_cap(pid_t pid, uint64_t cap);
int             sandbox_grant_cap(pid_t pid, uint64_t cap);
int             sandbox_revoke_cap(pid_t pid, uint64_t cap);
sandbox_ctx_t*  sandbox_get(pid_t pid);
void            sandbox_audit_log(pid_t pid, const char* action, int allowed);

/* ── IPC API ── */
void            ipc_init(void);
int             ipc_port_create(const char* name, uint64_t required_caps);
int             ipc_port_destroy(uint32_t port);
int             ipc_send(uint32_t port, const void* data, uint32_t size, ipc_msg_type_t type);
int             ipc_receive(uint32_t port, ipc_message_t* msg, uint32_t timeout_ms);
int             ipc_port_lookup(const char* name);

/* ── Shared Memory API ── */
int             shm_create(size_t size, uint32_t flags);
int             shm_attach(uint32_t id, pid_t client);
void*           shm_map(uint32_t id, pid_t pid);
int             shm_detach(uint32_t id, pid_t pid);
int             shm_destroy(uint32_t id);

#endif
