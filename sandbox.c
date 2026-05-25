/* Copyright (c) 2026 SynChanCyberSecurity LLC. All Rights Reserved. */
/*
 * Crimson OS - App Sandbox & IPC Implementation
 * 
 * Enforces capability-based access control per process.
 * Provides message-passing IPC and shared memory regions.
 * Every userland app runs inside a sandbox with restricted caps.
 */

#include <crimson/types.h>
#include <crimson/printk.h>
#include <crimson/spinlock.h>
#include <crimson/process.h>
#include <crimson/memory.h>
#include <crimson/timer.h>
#include <crimson/string.h>
#include <crimson/sandbox.h>

/* ═══════════════════════════════════════════════════════════
 *  SANDBOX SUBSYSTEM
 * ═══════════════════════════════════════════════════════════ */

#define MAX_SANDBOXES   128

static sandbox_ctx_t  g_sandboxes[MAX_SANDBOXES];
static spinlock_t     sandbox_lock;
static uint32_t       sandbox_count = 0;
static uint32_t       sandbox_violations_total = 0;

void sandbox_init(void)
{
    spin_lock_init(&sandbox_lock);
    kmemset(g_sandboxes, 0, sizeof(g_sandboxes));
    printk(KERN_INFO "[SANDBOX] App isolation subsystem initialized\n");
    printk(KERN_INFO "[SANDBOX] Max sandboxes: %d, memory limit per app: %lu MB\n",
           MAX_SANDBOXES, SANDBOX_MAX_MEMORY / (1024*1024));
}

sandbox_ctx_t* sandbox_create(pid_t pid, sandbox_level_t level, const char* app_id)
{
    unsigned long flags;
    spin_lock_irqsave(&sandbox_lock, flags);
    
    if (sandbox_count >= MAX_SANDBOXES) {
        spin_unlock_irqrestore(&sandbox_lock, flags);
        printk(KERN_ERR "[SANDBOX] Maximum sandbox count reached\n");
        return NULL;
    }
    
    /* Find free slot */
    sandbox_ctx_t* ctx = NULL;
    for (int i = 0; i < MAX_SANDBOXES; i++) {
        if (g_sandboxes[i].level == SANDBOX_NONE &&
            g_sandboxes[i].mem_base == 0) {
            ctx = &g_sandboxes[i];
            break;
        }
    }
    
    if (!ctx) {
        spin_unlock_irqrestore(&sandbox_lock, flags);
        return NULL;
    }
    
    /* Initialize sandbox context */
    kmemset(ctx, 0, sizeof(*ctx));
    ctx->level = level;
    kstrncpy(ctx->app_id, app_id, sizeof(ctx->app_id) - 1);
    
    /* Set default capabilities by level */
    switch (level) {
    case SANDBOX_SYSTEM:
        ctx->cap_granted = CAPS_SYSTEM & ~CAP_ADMIN;
        ctx->mem_limit = SANDBOX_MAX_MEMORY;
        break;
    case SANDBOX_USER:
        ctx->cap_granted = CAPS_USER_APP;
        ctx->mem_limit = SANDBOX_MAX_MEMORY / 2;
        break;
    case SANDBOX_UNTRUSTED:
        ctx->cap_granted = CAP_DISPLAY | CAP_IPC;
        ctx->mem_limit = SANDBOX_MAX_MEMORY / 4;
        break;
    default:
        ctx->cap_granted = CAPS_SYSTEM;
        ctx->mem_limit = SANDBOX_MAX_MEMORY;
        break;
    }
    
    /* Allocate isolated memory region via VMM */
    ctx->mem_base = (uintptr_t)kmalloc(ctx->mem_limit);
    if (!ctx->mem_base) {
        kmemset(ctx, 0, sizeof(*ctx));
        spin_unlock_irqrestore(&sandbox_lock, flags);
        printk(KERN_ERR "[SANDBOX] Failed to allocate memory for sandbox\n");
        return NULL;
    }
    
    /* Set up private data directory */
    ksnprintf(ctx->data_dir, sizeof(ctx->data_dir),
              "/data/apps/%s", app_id);
    
    sandbox_count++;
    spin_unlock_irqrestore(&sandbox_lock, flags);
    
    printk(KERN_INFO "[SANDBOX] Created sandbox for '%s' (PID %d, level %d)\n",
           app_id, pid, level);
    printk(KERN_INFO "[SANDBOX]   Caps: 0x%016llx, Mem: %lu KB\n",
           ctx->cap_granted, ctx->mem_limit / 1024);
    
    sandbox_audit_log(pid, "sandbox_create", 1);
    return ctx;
}

void sandbox_destroy(pid_t pid)
{
    unsigned long flags;
    spin_lock_irqsave(&sandbox_lock, flags);
    
    sandbox_ctx_t* ctx = sandbox_get(pid);
    if (ctx) {
        if (ctx->mem_base)
            kfree((void*)ctx->mem_base);
        
        printk(KERN_INFO "[SANDBOX] Destroyed sandbox '%s' "
               "(violations: %u, denied: %llu)\n",
               ctx->app_id, ctx->violations, ctx->cap_denied);
        
        kmemset(ctx, 0, sizeof(*ctx));
        sandbox_count--;
    }
    
    spin_unlock_irqrestore(&sandbox_lock, flags);
}

int sandbox_check_cap(pid_t pid, uint64_t cap)
{
    sandbox_ctx_t* ctx = sandbox_get(pid);
    
    /* No sandbox = kernel/root, always allowed */
    if (!ctx || ctx->level == SANDBOX_NONE)
        return 1;
    
    int allowed = (ctx->cap_granted & cap) == cap;
    
    if (allowed) {
        ctx->cap_used |= cap;
    } else {
        ctx->cap_denied |= cap;
        ctx->violations++;
        sandbox_violations_total++;
        sandbox_audit_log(pid, "cap_denied", 0);
        printk(KERN_WARN "[SANDBOX] PID %d: capability 0x%llx denied "
               "(granted: 0x%016llx)\n",
               pid, cap, ctx->cap_granted);
    }
    
    return allowed;
}

int sandbox_grant_cap(pid_t pid, uint64_t cap)
{
    sandbox_ctx_t* ctx = sandbox_get(pid);
    if (!ctx) return -1;
    
    /* Only SYSTEM level can grant caps dynamically */
    struct process* current = process_get_by_pid(current_pid());
    if (current && !(current->cap[0] & CAP_ADMIN)) {
        sandbox_audit_log(current_pid(), "grant_cap_denied", 0);
        return -1;
    }
    
    ctx->cap_granted |= cap;
    sandbox_audit_log(pid, "cap_granted", 1);
    printk(KERN_INFO "[SANDBOX] Granted cap 0x%llx to PID %d\n", cap, pid);
    return 0;
}

int sandbox_revoke_cap(pid_t pid, uint64_t cap)
{
    sandbox_ctx_t* ctx = sandbox_get(pid);
    if (!ctx) return -1;
    
    ctx->cap_granted &= ~cap;
    sandbox_audit_log(pid, "cap_revoked", 1);
    return 0;
}

sandbox_ctx_t* sandbox_get(pid_t pid)
{
    /* Linear scan — could be optimized with PID hash table */
    struct process* proc = process_get_by_pid(pid);
    if (!proc) return NULL;
    
    for (int i = 0; i < MAX_SANDBOXES; i++) {
        if (g_sandboxes[i].level != SANDBOX_NONE &&
            g_sandboxes[i].mem_base != 0) {
            return &g_sandboxes[i];
        }
    }
    return NULL;
}

void sandbox_audit_log(pid_t pid, const char* action, int allowed)
{
    printk(KERN_AUDIT "[SANDBOX] PID %d: %s -> %s\n",
           pid, action, allowed ? "ALLOW" : "DENY");
}


/* ═══════════════════════════════════════════════════════════
 *  IPC SUBSYSTEM — Message Passing
 * ═══════════════════════════════════════════════════════════ */

static ipc_port_t   g_ipc_ports[IPC_PORT_MAX];
static spinlock_t    ipc_lock;
static uint32_t      ipc_next_port = 1;
static uint32_t      ipc_msg_id = 1;

void ipc_init(void)
{
    spin_lock_init(&ipc_lock);
    kmemset(g_ipc_ports, 0, sizeof(g_ipc_ports));
    
    printk(KERN_INFO "[IPC] Message-passing subsystem initialized\n");
    printk(KERN_INFO "[IPC] Max ports: %d, queue depth: %d, max msg: %d bytes\n",
           IPC_PORT_MAX, IPC_QUEUE_DEPTH, IPC_MSG_MAX_SIZE);
}

int ipc_port_create(const char* name, uint64_t required_caps)
{
    unsigned long flags;
    spin_lock_irqsave(&ipc_lock, flags);
    
    /* Check for duplicate name */
    for (int i = 0; i < IPC_PORT_MAX; i++) {
        if (g_ipc_ports[i].owner != 0 &&
            kstrcmp(g_ipc_ports[i].name, name) == 0) {
            spin_unlock_irqrestore(&ipc_lock, flags);
            return -1; /* Already exists */
        }
    }
    
    /* Find free slot */
    ipc_port_t* port = NULL;
    int idx = -1;
    for (int i = 0; i < IPC_PORT_MAX; i++) {
        if (g_ipc_ports[i].owner == 0) {
            port = &g_ipc_ports[i];
            idx = i;
            break;
        }
    }
    
    if (!port) {
        spin_unlock_irqrestore(&ipc_lock, flags);
        return -1;
    }
    
    kmemset(port, 0, sizeof(*port));
    port->port_id = ipc_next_port++;
    port->owner = current_pid();
    port->required_caps = required_caps;
    kstrncpy(port->name, name, sizeof(port->name) - 1);
    spin_lock_init(&port->lock);
    
    spin_unlock_irqrestore(&ipc_lock, flags);
    
    printk(KERN_INFO "[IPC] Port '%s' created (id=%u, owner=%d, caps=0x%llx)\n",
           name, port->port_id, port->owner, required_caps);
    
    return port->port_id;
}

int ipc_port_destroy(uint32_t port_id)
{
    unsigned long flags;
    spin_lock_irqsave(&ipc_lock, flags);
    
    for (int i = 0; i < IPC_PORT_MAX; i++) {
        if (g_ipc_ports[i].port_id == port_id) {
            pid_t caller = current_pid();
            if (g_ipc_ports[i].owner != caller &&
                !sandbox_check_cap(caller, CAP_ADMIN)) {
                spin_unlock_irqrestore(&ipc_lock, flags);
                return -1;
            }
            printk(KERN_INFO "[IPC] Port '%s' destroyed\n", g_ipc_ports[i].name);
            kmemset(&g_ipc_ports[i], 0, sizeof(ipc_port_t));
            spin_unlock_irqrestore(&ipc_lock, flags);
            return 0;
        }
    }
    
    spin_unlock_irqrestore(&ipc_lock, flags);
    return -1;
}

int ipc_send(uint32_t port_id, const void* data, uint32_t size, ipc_msg_type_t type)
{
    if (size > IPC_MSG_MAX_SIZE) return -1;
    
    /* Find port */
    ipc_port_t* port = NULL;
    for (int i = 0; i < IPC_PORT_MAX; i++) {
        if (g_ipc_ports[i].port_id == port_id) {
            port = &g_ipc_ports[i];
            break;
        }
    }
    if (!port) return -1;
    
    /* Check sender capabilities */
    pid_t sender = current_pid();
    if (port->required_caps && !sandbox_check_cap(sender, port->required_caps)) {
        printk(KERN_WARN "[IPC] PID %d lacks caps for port '%s'\n",
               sender, port->name);
        return -1;
    }
    
    unsigned long flags;
    spin_lock_irqsave(&port->lock, flags);
    
    if (port->count >= IPC_QUEUE_DEPTH) {
        spin_unlock_irqrestore(&port->lock, flags);
        return -1; /* Queue full */
    }
    
    /* Enqueue message */
    ipc_message_t* msg = &port->queue[port->tail];
    msg->id = ipc_msg_id++;
    msg->type = type;
    msg->sender = sender;
    msg->receiver = port->owner;
    msg->port = port_id;
    msg->size = size;
    msg->timestamp = timer_get_ticks();
    if (data && size > 0)
        kmemcpy(msg->data, data, size);
    
    port->tail = (port->tail + 1) % IPC_QUEUE_DEPTH;
    port->count++;
    
    spin_unlock_irqrestore(&port->lock, flags);
    
    /* Wake any waiters */
    /* wait_queue_wake(&port->waiters); */
    
    return msg->id;
}

int ipc_receive(uint32_t port_id, ipc_message_t* out_msg, uint32_t timeout_ms)
{
    ipc_port_t* port = NULL;
    for (int i = 0; i < IPC_PORT_MAX; i++) {
        if (g_ipc_ports[i].port_id == port_id) {
            port = &g_ipc_ports[i];
            break;
        }
    }
    if (!port) return -1;
    
    /* Only owner can receive */
    if (port->owner != current_pid()) return -1;
    
    uint64_t deadline = timer_get_ticks() + (timeout_ms * 1000);
    
    while (1) {
        unsigned long flags;
        spin_lock_irqsave(&port->lock, flags);
        
        if (port->count > 0) {
            kmemcpy(out_msg, &port->queue[port->head], sizeof(ipc_message_t));
            port->head = (port->head + 1) % IPC_QUEUE_DEPTH;
            port->count--;
            spin_unlock_irqrestore(&port->lock, flags);
            return 0;
        }
        
        spin_unlock_irqrestore(&port->lock, flags);
        
        if (timeout_ms == 0) return -1;
        if (timer_get_ticks() >= deadline) return -1;
        
        /* Yield CPU while waiting */
        /* sched_yield(); */
    }
}

int ipc_port_lookup(const char* name)
{
    for (int i = 0; i < IPC_PORT_MAX; i++) {
        if (g_ipc_ports[i].owner != 0 &&
            kstrcmp(g_ipc_ports[i].name, name) == 0) {
            return g_ipc_ports[i].port_id;
        }
    }
    return -1;
}


/* ═══════════════════════════════════════════════════════════
 *  SHARED MEMORY
 * ═══════════════════════════════════════════════════════════ */

#define SHM_FLAG_READONLY   (1 << 0)
#define SHM_FLAG_READWRITE  (1 << 1)

static shm_region_t  g_shm_regions[SHM_MAX_REGIONS];
static spinlock_t    shm_lock;
static uint32_t      shm_next_id = 1;

int shm_create(size_t size, uint32_t flags)
{
    if (size == 0 || size > (16 * 1024 * 1024)) return -1;
    
    unsigned long irqf;
    spin_lock_irqsave(&shm_lock, irqf);
    
    shm_region_t* r = NULL;
    for (int i = 0; i < SHM_MAX_REGIONS; i++) {
        if (g_shm_regions[i].id == 0) {
            r = &g_shm_regions[i];
            break;
        }
    }
    
    if (!r) {
        spin_unlock_irqrestore(&shm_lock, irqf);
        return -1;
    }
    
    /* Allocate physically contiguous pages */
    void* mem = kmalloc(size);
    if (!mem) {
        spin_unlock_irqrestore(&shm_lock, irqf);
        return -1;
    }
    
    r->id = shm_next_id++;
    r->phys_addr = (uintptr_t)mem;
    r->size = size;
    r->owner = current_pid();
    r->client_count = 0;
    r->flags = flags;
    
    spin_unlock_irqrestore(&shm_lock, irqf);
    
    printk(KERN_INFO "[SHM] Region %u created: %lu bytes, owner PID %d\n",
           r->id, size, r->owner);
    return r->id;
}

int shm_attach(uint32_t id, pid_t client)
{
    for (int i = 0; i < SHM_MAX_REGIONS; i++) {
        if (g_shm_regions[i].id == id) {
            shm_region_t* r = &g_shm_regions[i];
            if (r->client_count >= 8) return -1;
            
            /* Check IPC capability */
            if (!sandbox_check_cap(client, CAP_IPC))
                return -1;
            
            r->clients[r->client_count++] = client;
            printk(KERN_INFO "[SHM] PID %d attached to region %u\n", client, id);
            return 0;
        }
    }
    return -1;
}

void* shm_map(uint32_t id, pid_t pid)
{
    for (int i = 0; i < SHM_MAX_REGIONS; i++) {
        if (g_shm_regions[i].id == id) {
            shm_region_t* r = &g_shm_regions[i];
            
            /* Verify access: must be owner or attached client */
            if (r->owner == pid) return (void*)r->phys_addr;
            for (uint32_t j = 0; j < r->client_count; j++) {
                if (r->clients[j] == pid)
                    return (void*)r->phys_addr;
            }
            return NULL;
        }
    }
    return NULL;
}

int shm_detach(uint32_t id, pid_t pid)
{
    for (int i = 0; i < SHM_MAX_REGIONS; i++) {
        if (g_shm_regions[i].id == id) {
            shm_region_t* r = &g_shm_regions[i];
            for (uint32_t j = 0; j < r->client_count; j++) {
                if (r->clients[j] == pid) {
                    r->clients[j] = r->clients[--r->client_count];
                    return 0;
                }
            }
            return -1;
        }
    }
    return -1;
}

int shm_destroy(uint32_t id)
{
    for (int i = 0; i < SHM_MAX_REGIONS; i++) {
        if (g_shm_regions[i].id == id) {
            shm_region_t* r = &g_shm_regions[i];
            if (r->owner != current_pid() &&
                !sandbox_check_cap(current_pid(), CAP_ADMIN))
                return -1;
            
            kfree((void*)r->phys_addr);
            kmemset(r, 0, sizeof(*r));
            return 0;
        }
    }
    return -1;
}
