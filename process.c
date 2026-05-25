/* Copyright (c) 2026 SynChanCyberSecurity LLC. All Rights Reserved. */
/*
 * Crimson OS - Process Management
 * 
 * Process lifecycle: create, fork, exec, wait, exit, kill
 * - Each process has: own address space, file descriptors, signal handlers
 * - PID allocation using bitmap for O(1) allocation
 * - Process table is a fixed-size array (configurable at compile time)
 * 
 * Process structure includes:
 * - CPU context (registers, stack pointer, program counter)
 * - Memory management (page directory)
 * - Scheduling state and statistics
 * - Security context (UID, GID, capabilities)
 * - Signal state
 * - File descriptor table pointer
 */

#include <crimson/types.h>
#include <crimson/process.h>
#include <crimson/scheduler.h>
#include <crimson/memory.h>
#include <crimson/spinlock.h>
#include <crimson/printk.h>
#include <crimson/asm.h>

/* Forward declarations for static functions */
static pid_t alloc_pid(void);
static void  free_pid(pid_t pid);
static void  process_cleanup(struct process* proc);

#include <crimson/signal.h>
#include <crimson/timer.h>

/* Maximum number of processes */
#define MAX_PROCESSES    1024
#define PID_BITMAP_SIZE  (MAX_PROCESSES / 64)

/* Process table */
static struct process process_table[MAX_PROCESSES];
static uint64_t pid_bitmap[PID_BITMAP_SIZE];
static uint32_t num_processes = 0;
static pid_t next_pid = 1;

/* Process table lock */
static spinlock_t proc_table_lock = SPINLOCK_INIT;

/* Zombie process list for reaping */
static struct process* zombie_list = NULL;
static spinlock_t zombie_lock = SPINLOCK_INIT;

/*
 * process_init - Initialize the process subsystem
 */
void process_init(void)
{
    /* Clear process table */
    memset(process_table, 0, sizeof(process_table));
    memset(pid_bitmap, 0, sizeof(pid_bitmap));
    
    /* PID 0 is reserved (idle/init) */
    pid_bitmap[0] |= 1;
    
    num_processes = 0;
    
    printk(KERN_INFO "Process: max %d concurrent processes\n", MAX_PROCESSES);
}

/*
 * process_create - Create a new kernel process
 * @attr: Process attributes
 * Returns: PID on success, negative on error
 */
pid_t process_create(const struct process_attr* attr)
{
    if (attr == NULL || attr->entry == NULL) {
        return -STATUS_INVAL;
    }
    
    spin_lock(&proc_table_lock);
    
    /* Find free process slot */
    int slot = -1;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (process_table[i].state == PROC_UNUSED) {
            slot = i;
            break;
        }
    }
    
    if (slot < 0) {
        spin_unlock(&proc_table_lock);
        printk(KERN_WARN "process_create: process table full\n");
        return -STATUS_NOMEM;
    }
    
    /* Allocate PID */
    pid_t pid = alloc_pid();
    if (pid < 0) {
        spin_unlock(&proc_table_lock);
        return -STATUS_NOMEM;
    }
    
    struct process* proc = &process_table[slot];
    memset(proc, 0, sizeof(struct process));
    
    /* Basic info */
    proc->pid = pid;
    proc->ppid = 0;  /* kernel parent for now */
    strncpy(proc->name, attr->name, sizeof(proc->name) - 1);
    proc->name[sizeof(proc->name) - 1] = '\0';
    
    /* State */
    proc->state = PROC_READY;
    proc->priority = attr->priority;
    proc->flags = attr->flags;
    proc->cpu = 0;
    
    /* CPU context
     * arch_context_switch and arch_jump_to_process use a COMPACT save area
     * that maps onto the beginning of cpu_context at hardcoded byte offsets:
     *   offset  0..72: callee-saved x19-x28 (left as 0 for new processes)
     *   offset 80:     x29 (frame pointer — 0 = base frame)
     *   offset 88:     x30 = entry point  (loaded into x30, branched via ret/blr)
     *   offset 96:     sp  = initial stack pointer
     * These map to ctx.x10, ctx.x11, ctx.x12 in the full cpu_context struct.
     */
    uintptr_t stack_top = (uintptr_t)kmalloc(attr->stack_size) + attr->stack_size;
    if (stack_top == (uintptr_t)NULL + attr->stack_size) {
        free_pid(pid);
        proc->state = PROC_UNUSED;
        spin_unlock(&proc_table_lock);
        return -STATUS_NOMEM;
    }
    /* Align stack to 16 bytes (AArch64 ABI requirement) */
    stack_top &= ~(uintptr_t)0xF;

    proc->ctx.x11 = (uintptr_t)attr->entry;  /* offset 88 = x30 slot → entry */
    proc->ctx.x12 = stack_top;               /* offset 96 = sp slot */

    /* Also keep the named fields consistent for debugging */
    proc->ctx.sp     = stack_top;
    proc->ctx.pc     = (uintptr_t)attr->entry;
    proc->ctx.pstate = 0x345;  /* EL1, interrupts enabled */
    
    /* Memory */
    if (attr->flags & PROC_FLAG_USER) {
        /* User process - allocate page directory */
        proc->pgd = (pgd_t*)pmm_alloc();
        if (proc->pgd == NULL) {
            kfree((void*)(proc->ctx.sp - attr->stack_size));
            free_pid(pid);
            proc->state = PROC_UNUSED;
            spin_unlock(&proc_table_lock);
            return -STATUS_NOMEM;
        }
        vmm_init(proc->pgd);
    } else {
        /* Kernel process - use kernel page tables */
        proc->pgd = NULL;  /* Uses TTBR1_EL1 */
    }
    
    proc->stack_base = proc->ctx.sp - attr->stack_size;
    proc->stack_size = attr->stack_size;
    
    /* Security context */
    proc->uid = 0;   /* root for kernel processes */
    proc->gid = 0;
    proc->euid = 0;
    proc->egid = 0;
    memset(proc->cap, 0xFF, sizeof(proc->cap));  /* All capabilities */
    
    /* Statistics */
    proc->start_time = timer_get_uptime_ms();
    proc->cpu_time = 0;
    proc->time_slice = 0;
    
    /* Signals */
    memset(proc->sig_handler, 0, sizeof(proc->sig_handler));
    proc->sig_pending = 0;
    proc->sig_blocked = 0;
    
    /* Scheduling */
    proc->sched_next = NULL;
    proc->wq = NULL;
    proc->wq_next = NULL;
    proc->children = NULL;
    proc->sibling = NULL;
    proc->parent = NULL;
    
    /* File descriptors - allocate table */
    proc->fd_table = kcalloc(1, sizeof(struct fd_table));
    if (proc->fd_table) {
        proc->fd_table->count = 0;
        proc->fd_table->max_fd = 256;
    }
    
    num_processes++;
    spin_unlock(&proc_table_lock);
    
    /* Add to scheduler */
    scheduler_add_process(proc);
    
    printk(KERN_DEBUG "Process: created PID %d (%s) prio=%d\n",
           pid, proc->name, proc->priority);
    
    return pid;
}

/*
 * process_exit - Terminate current process
 * @code: Exit code
 */
void process_exit(int code)
{
    uint32_t cpu = cpu_get_id();
    struct process* current = scheduler_get_current();
    
    if (current == NULL) {
        kernel_panic("process_exit: no current process");
    }
    
    spin_lock(&proc_table_lock);
    
    current->exit_code = code;
    current->state = PROC_ZOMBIE;
    
    /* Reparent children to init (PID 1) */
    struct process* child = current->children;
    while (child != NULL) {
        child->ppid = 1;
        child = child->sibling;
    }
    
    /* Wake up parent if waiting */
    if (current->parent && current->parent->state == PROC_SLEEPING) {
        /* Signal parent */
    }
    
    /* Add to zombie list for reaping */
    spin_lock(&zombie_lock);
    current->sibling = zombie_list;
    zombie_list = current;
    spin_unlock(&zombie_lock);
    
    num_processes--;
    spin_unlock(&proc_table_lock);
    
    printk(KERN_INFO "Process: PID %d (%s) exited with code %d\n",
           current->pid, current->name, code);
    
    /* Yield - scheduler will clean up */
    scheduler_yield();
    
    /* Should not reach here */
    kernel_panic("process_exit: returned from yield");
}

/*
 * process_kill - Send signal to a process
 * @pid: Target process
 * @sig: Signal number
 */
int process_kill(pid_t pid, int sig)
{
    if (pid <= 0 || pid >= MAX_PROCESSES) return -STATUS_INVAL;
    if (sig < 0 || sig >= NUM_SIGNALS) return -STATUS_INVAL;
    
    spin_lock(&proc_table_lock);
    
    struct process* proc = &process_table[pid - 1];
    if (proc->state == PROC_UNUSED || proc->state == PROC_ZOMBIE) {
        spin_unlock(&proc_table_lock);
        return -STATUS_NOTFOUND;
    }
    
    /* Check permissions */
    struct process* current = scheduler_get_current();
    if (current->euid != 0 && current->euid != proc->uid) {
        spin_unlock(&proc_table_lock);
        return -STATUS_PERM;
    }
    
    /* Deliver signal */
    proc->sig_pending |= (1ULL << sig);
    
    /* If process is sleeping, wake it up */
    if (proc->state == PROC_SLEEPING) {
        proc->state = PROC_READY;
        scheduler_add_process(proc);
    }
    
    spin_unlock(&proc_table_lock);
    
    return STATUS_OK;
}

/*
 * process_wait - Wait for child process to exit
 * @pid: Child PID (or -1 for any child)
 * @status: Pointer to store exit status
 * @options: Wait options
 */
pid_t process_wait(pid_t pid, int* status, int options)
{
    struct process* current = scheduler_get_current();
    
    while (1) {
        spin_lock(&proc_table_lock);
        
        /* Check for zombie children */
        struct process* child = current->children;
        struct process* prev = NULL;
        
        while (child != NULL) {
            if ((pid == -1 || child->pid == pid) && child->state == PROC_ZOMBIE) {
                /* Found zombie child */
                pid_t cpid = child->pid;
                if (status) *status = child->exit_code;
                
                /* Remove from children list */
                if (prev) {
                    prev->sibling = child->sibling;
                } else {
                    current->children = child->sibling;
                }
                
                /* Free resources */
                process_cleanup(child);
                
                spin_unlock(&proc_table_lock);
                return cpid;
            }
            prev = child;
            child = child->sibling;
        }
        
        /* No zombie children found */
        if (options & WNOHANG) {
            spin_unlock(&proc_table_lock);
            return 0;
        }
        
        /* No children at all? */
        if (current->children == NULL) {
            spin_unlock(&proc_table_lock);
            return -STATUS_NOTFOUND;
        }
        
        spin_unlock(&proc_table_lock);
        
        /* Sleep until a child exits */
        scheduler_yield();
    }
}

/*
 * process_get_by_pid - Look up process by PID
 */
struct process* process_get_by_pid(pid_t pid)
{
    if (pid <= 0 || pid > MAX_PROCESSES) return NULL;
    
    struct process* proc = &process_table[pid - 1];
    if (proc->state == PROC_UNUSED) return NULL;
    
    return proc;
}

/*
 * process_list - Print all processes (like ps)
 */
void process_list(void)
{
    printk("\n  PID  PPID  STATE   CPU   PRIO    TIME  NAME\n");
    printk("  ---  ----  -----   ---   ----    ----  ----\n");
    
    spin_lock(&proc_table_lock);
    
    for (int i = 0; i < MAX_PROCESSES; i++) {
        struct process* p = &process_table[i];
        if (p->state == PROC_UNUSED) continue;
        
        const char* state_str;
        switch (p->state) {
            case PROC_RUNNING:  state_str = "RUN  "; break;
            case PROC_READY:    state_str = "READY"; break;
            case PROC_SLEEPING: state_str = "SLEEP"; break;
            case PROC_ZOMBIE:   state_str = "ZOMB"; break;
            case PROC_STOPPED:  state_str = "STOP "; break;
            default:            state_str = "???  "; break;
        }
        
        const char* prio_str;
        switch (p->priority) {
            case PRIO_REALTIME: prio_str = "RT  "; break;
            case PRIO_HIGH:     prio_str = "HIGH"; break;
            case PRIO_NORMAL:   prio_str = "NORM"; break;
            case PRIO_LOW:      prio_str = "LOW "; break;
            case PRIO_IDLE:     prio_str = "IDLE"; break;
            default:            prio_str = "?   "; break;
        }
        
        printk("  %3d  %4d  %s   %3d   %s  %5lu  %s\n",
               p->pid, p->ppid, state_str, p->cpu,
               prio_str, p->cpu_time, p->name);
    }
    
    spin_unlock(&proc_table_lock);
    printk("\n");
}

/* ─── Internal Functions ─── */

/*
 * alloc_pid - Allocate a unique process ID
 */
static pid_t alloc_pid(void)
{
    for (int i = 0; i < PID_BITMAP_SIZE; i++) {
        if (pid_bitmap[i] != ~0ULL) {
            int bit = __builtin_ctzll(~pid_bitmap[i]);
            pid_bitmap[i] |= (1ULL << bit);
            return i * 64 + bit;
        }
    }
    return -1;
}

/*
 * free_pid - Release a process ID
 */
static void free_pid(pid_t pid)
{
    if (pid < 0 || pid >= MAX_PROCESSES) return;
    int idx = pid / 64;
    int bit = pid % 64;
    pid_bitmap[idx] &= ~(1ULL << bit);
}

/*
 * process_cleanup - Free all resources associated with a process
 */
static void process_cleanup(struct process* proc)
{
    if (proc == NULL) return;
    
    /* Free stack */
    if (proc->stack_base != 0) {
        /* Stack was allocated from heap */
        kfree((void*)proc->stack_base);
    }
    
    /* Free page directory (user processes) */
    if (proc->pgd != NULL) {
        /* Walk and free all page table levels */
        vmm_free_all(proc->pgd);
        pmm_free((uintptr_t)proc->pgd);
    }
    
    /* Free file descriptor table */
    if (proc->fd_table) {
        kfree(proc->fd_table);
    }
    
    /* Free PID */
    free_pid(proc->pid);
    
    /* Mark slot as unused */
    proc->state = PROC_UNUSED;
}

/*
 * process_reap_zombies - Clean up zombie processes
 * Called periodically by scheduler
 */
void process_reap_zombies(void)
{
    spin_lock(&zombie_lock);
    
    struct process* proc = zombie_list;
    struct process* prev = NULL;
    
    while (proc != NULL) {
        struct process* next = proc->sibling;
        
        /* Check if parent has reaped this child */
        if (proc->state == PROC_UNUSED) {
            if (prev) {
                prev->sibling = next;
            } else {
                zombie_list = next;
            }
        } else {
            prev = proc;
        }
        
        proc = next;
    }
    
    spin_unlock(&zombie_lock);
}

/*
 * current_pid - Get the PID of the currently running process
 */
pid_t current_pid(void)
{
    struct process* current = scheduler_get_current();
    if (current)
        return current->pid;
    return 0;  /* Kernel/boot context */
}
