/* Copyright (c) 2026 SynChanCyberSecurity LLC. All Rights Reserved. */
/*
 * Crimson OS - Process Management Header
 */

#ifndef _CRIMSON_PROCESS_H
#define _CRIMSON_PROCESS_H

#include <crimson/types.h>
#include <crimson/memory.h>
#include <crimson/spinlock.h>

/* Process states */
enum proc_state {
    PROC_UNUSED = 0,
    PROC_READY,
    PROC_RUNNING,
    PROC_SLEEPING,
    PROC_STOPPED,
    PROC_ZOMBIE,
    PROC_DEAD,
};

/* Process priorities */
enum proc_priority {
    PRIO_REALTIME = 0,
    PRIO_HIGH,
    PRIO_NORMAL,
    PRIO_LOW,
    PRIO_IDLE,
};

/* Process flags */
#define PROC_FLAG_KERNEL    (1 << 0)
#define PROC_FLAG_USER      (1 << 1)
#define PROC_FLAG_DAEMON    (1 << 2)

/* Forward declarations */
struct process;

/* File descriptor table */
struct fd_table {
    int count;
    int max_fd;
};

/* CPU context (saved during context switch) */
struct cpu_context {
    uint64_t x0, x1, x2, x3, x4, x5, x6, x7;
    uint64_t x8, x9, x10, x11, x12, x13, x14, x15;
    uint64_t x16, x17, x18, x19, x20, x21, x22, x23;
    uint64_t x24, x25, x26, x27, x28, x29;
    uint64_t x30;
    uint64_t sp;
    uint64_t pc;
    uint64_t pstate;
};

/* Process Control Block */
struct process {
    pid_t pid;
    pid_t ppid;
    char name[32];
    enum proc_state state;
    int priority;
    uint32_t flags;
    uint32_t cpu;
    struct cpu_context ctx;
    pgd_t* pgd;
    uintptr_t stack_base;
    size_t stack_size;
    struct process* sched_next;
    uint64_t time_slice;
    uint64_t cpu_time;
    uint64_t start_time;
    wait_queue_t* wq;
    struct process* wq_next;
    struct process* parent;
    struct process* children;
    struct process* sibling;
    int exit_code;
    uint32_t uid;
    uint32_t gid;
    uint32_t euid;
    uint32_t egid;
    uint64_t cap[2];
    void (*sig_handler[64])(int);
    uint64_t sig_pending;
    uint64_t sig_blocked;
    struct fd_table* fd_table;
};

/* Process creation attributes */
struct process_attr {
    const char* name;
    void (*entry)(void);
    int priority;
    size_t stack_size;
    uint32_t flags;
};

/* Wait options */
#define WNOHANG     1

/* Subsystem functions */
void process_init(void);
pid_t process_create(const struct process_attr* attr);
void process_exit(int code);
int process_kill(pid_t pid, int sig);
pid_t process_wait(pid_t pid, int* status, int options);
struct process* process_get_by_pid(pid_t pid);
void process_list(void);
void process_reap_zombies(void);

/* Built-in process functions */

/* Get current process PID */
pid_t current_pid(void);

#endif
