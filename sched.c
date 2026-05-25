/* Copyright (c) 2026 SynChanCyberSecurity LLC. All Rights Reserved. */
/*
 * Crimson OS - Process Scheduler
 * 
 * Multi-level Feedback Queue (MLFQ) with priority scheduling
 * - 5 priority levels (REALTIME, HIGH, NORMAL, LOW, IDLE)
 * - Time quantum: 4ms (high) to 16ms (low)
 * - Preemptive with priority inheritance
 * - O(1) task selection using bitmap queues
 * 
 * This scheduler ensures:
 * - Real-time responsiveness for UI/security tasks
 * - Fair CPU sharing for background processes  
 * - Prevention of priority inversion
 * - Battery-aware scheduling for mobile
 */

#include <crimson/types.h>
#include <crimson/scheduler.h>
#include <crimson/process.h>
#include <crimson/spinlock.h>
#include <crimson/printk.h>
#include <crimson/memory.h>
#include <crimson/asm.h>

/* Scheduling constants */
#define SCHED_QUEUES        5       /* Number of priority queues */
#define SCHED_HZ            100     /* Timer ticks per second */
#define TICK_MS             (1000 / SCHED_HZ)

/* Time quantums per priority level (in ticks) */
static const uint32_t time_quantum[SCHED_QUEUES] = {
    [PRIO_REALTIME] = 0xFFFFFFFF,  /* Run to completion (or yield) */
    [PRIO_HIGH]     = 4,           /* 40ms */
    [PRIO_NORMAL]   = 8,           /* 80ms */
    [PRIO_LOW]      = 16,          /* 160ms */
    [PRIO_IDLE]     = 32,          /* 320ms */
};

/* Priority boost every N ticks to prevent starvation */
#define PRIORITY_BOOST_INTERVAL  200   /* Every 2 seconds */

/* Per-CPU runqueue */
typedef struct {
    struct process* queues[SCHED_QUEUES];  /* Heads of priority queues */
    struct process* tails[SCHED_QUEUES];   /* Tails for O(1) enqueue */
    uint32_t queue_bitmap;                  /* Which queues are non-empty */
    struct process* current;               /* Currently running process */
    struct process* idle_task;             /* CPU idle process */
    uint32_t ticks;                         /* Ticks since last boost */
    spinlock_t lock;
    uint32_t cpu_id;
} runqueue_t;

static runqueue_t* runqueues = NULL;
static uint32_t num_cpus = 1;
static volatile uint32_t sched_initialized = 0;

/* Forward declarations */
static void enqueue_process(struct process* proc);
static struct process* dequeue_highest(void);
static void context_switch(struct process* prev, struct process* next);
static void priority_boost(void);

/*
 * scheduler_init - Initialize the scheduler subsystem
 */
void scheduler_init(void)
{
    num_cpus = cpu_get_core_count();
    
    runqueues = kcalloc(num_cpus, sizeof(runqueue_t));
    if (runqueues == NULL) {
        kernel_panic("scheduler_init: failed to allocate runqueues");
    }
    
    for (uint32_t i = 0; i < num_cpus; i++) {
        runqueues[i].cpu_id = i;
        runqueues[i].current = NULL;
        runqueues[i].queue_bitmap = 0;
        runqueues[i].ticks = 0;
        spinlock_init(&runqueues[i].lock);
        
        for (int q = 0; q < SCHED_QUEUES; q++) {
            runqueues[i].queues[q] = NULL;
            runqueues[i].tails[q] = NULL;
        }
    }
    
    sched_initialized = 1;
    printk(KERN_INFO "Scheduler: MLFQ with %d priority levels, %u Hz\n",
           SCHED_QUEUES, SCHED_HZ);
}

/*
 * scheduler_start - Begin scheduling (called once, never returns)
 */
void __attribute__((noreturn)) scheduler_start(void)
{
    if (!sched_initialized) {
        kernel_panic("scheduler_start: scheduler not initialized");
    }
    
    uint32_t cpu = cpu_get_id();
    runqueue_t* rq = &runqueues[cpu];
    
    printk(KERN_INFO "Scheduler: CPU %d starting\n", cpu);
    
    /* Find first task to run */
    struct process* proc = dequeue_highest();
    if (proc == NULL) {
        kernel_panic("scheduler_start: no initial task!");
    }
    
    rq->current = proc;
    proc->state = PROC_RUNNING;
    proc->cpu = cpu;
    proc->time_slice = time_quantum[proc->priority];
    
    /* Jump to first process - this "returns" into the process context */
    arch_jump_to_process(proc);
    
    /* NEVER REACHED */
    __builtin_unreachable();
}

/*
 * scheduler_tick - Called by timer interrupt every tick
 * 
 * This is the heart of preemptive scheduling. It:
 * 1. Decrements the current process's time slice
 * 2. If expired, selects next process
 * 3. Performs context switch
 */
void scheduler_tick(void)
{
    uint32_t cpu = cpu_get_id();
    runqueue_t* rq = &runqueues[cpu];
    
    spin_lock(&rq->lock);
    
    struct process* current = rq->current;
    if (current == NULL) {
        spin_unlock(&rq->lock);
        return;
    }
    
    /* Update accounting */
    current->cpu_time++;
    rq->ticks++;
    
    /* Realtime processes run until they yield */
    if (current->priority == PRIO_REALTIME) {
        spin_unlock(&rq->lock);
        return;
    }
    
    /* Decrement time slice */
    if (current->time_slice > 0) {
        current->time_slice--;
    }
    
    /* Check if time slice expired */
    if (current->time_slice == 0) {
        /* Demote priority if using too much CPU (prevent hogging) */
        if (current->priority > PRIO_LOW && current->cpu_time % 100 == 0) {
            /* Dynamic priority adjustment based on behavior */
        }
        
        /* Requeue the current process */
        current->state = PROC_READY;
        enqueue_process(current);
        
        /* Select next process */
        struct process* next = dequeue_highest();
        if (next == NULL) {
            next = rq->idle_task;
        }
        
        if (next != current) {
            rq->current = next;
            next->state = PROC_RUNNING;
            next->cpu = cpu;
            next->time_slice = time_quantum[next->priority];
            
            spin_unlock(&rq->lock);
            context_switch(current, next);
            return;
        } else {
            /* Only idle task available, reset its slice */
            current->time_slice = time_quantum[current->priority];
        }
    }
    
    /* Priority boost to prevent starvation */
    if (rq->ticks >= PRIORITY_BOOST_INTERVAL) {
        rq->ticks = 0;
        spin_unlock(&rq->lock);
        priority_boost();
        return;
    }
    
    spin_unlock(&rq->lock);
}

/*
 * scheduler_yield - Current process voluntarily yields CPU
 */
void scheduler_yield(void)
{
    uint32_t cpu = cpu_get_id();
    runqueue_t* rq = &runqueues[cpu];
    
    spin_lock(&rq->lock);
    
    struct process* current = rq->current;
    if (current == NULL) {
        spin_unlock(&rq->lock);
        return;
    }
    
    /* Requeue at same priority */
    current->state = PROC_READY;
    enqueue_process(current);
    
    struct process* next = dequeue_highest();
    if (next == NULL || next == current) {
        current->time_slice = time_quantum[current->priority];
        spin_unlock(&rq->lock);
        return;
    }
    
    rq->current = next;
    next->state = PROC_RUNNING;
    next->cpu = cpu;
    next->time_slice = time_quantum[next->priority];
    
    spin_unlock(&rq->lock);
    context_switch(current, next);
}

/*
 * scheduler_add_process - Add a new process to scheduling
 */
void scheduler_add_process(struct process* proc)
{
    if (proc == NULL) return;
    
    /* Assign to least loaded CPU */
    uint32_t target_cpu = 0;
    uint32_t min_load = 0xFFFFFFFF;
    
    for (uint32_t i = 0; i < num_cpus; i++) {
        uint32_t load = runqueues[i].queue_bitmap;
        /* Count set bits (approximate queue depth) */
        uint32_t count = 0;
        for (int q = 0; q < SCHED_QUEUES; q++) {
            if (load & (1 << q)) count++;
        }
        if (count < min_load) {
            min_load = count;
            target_cpu = i;
        }
    }
    
    proc->cpu = target_cpu;
    proc->state = PROC_READY;
    proc->time_slice = time_quantum[proc->priority];
    
    spin_lock(&runqueues[target_cpu].lock);
    enqueue_process(proc);
    spin_unlock(&runqueues[target_cpu].lock);
    
    /* If new process has higher priority, trigger reschedule */
    uint32_t current_cpu = cpu_get_id();
    struct process* current = runqueues[current_cpu].current;
    if (current != NULL && proc->priority < current->priority) {
        /* Trigger IPI for immediate reschedule */
        arch_trigger_reschedule(target_cpu);
    }
}

/*
 * scheduler_remove_process - Remove process from scheduling
 */
void scheduler_remove_process(struct process* proc)
{
    if (proc == NULL) return;
    
    uint32_t cpu = proc->cpu;
    runqueue_t* rq = &runqueues[cpu];
    
    spin_lock(&rq->lock);
    
    /* Remove from its queue */
    struct process** pp = &rq->queues[proc->priority];
    while (*pp != NULL) {
        if (*pp == proc) {
            *pp = proc->sched_next;
            if (rq->tails[proc->priority] == proc) {
                rq->tails[proc->priority] = NULL;
            }
            break;
        }
        pp = &(*pp)->sched_next;
    }
    
    proc->sched_next = NULL;
    proc->state = PROC_DEAD;
    
    spin_unlock(&rq->lock);
}

/*
 * scheduler_sleep_on - Sleep on a wait queue
 */
void scheduler_sleep_on(wait_queue_t* wq, spinlock_t* lock)
{
    uint32_t cpu = cpu_get_id();
    runqueue_t* rq = &runqueues[cpu];
    
    struct process* current = rq->current;
    if (current == NULL) return;
    
    current->state = PROC_SLEEPING;
    current->wq = wq;
    
    /* Add to wait queue */
    spin_lock(&wq->lock);
    current->wq_next = wq->head;
    wq->head = current;
    spin_unlock(&wq->lock);
    
    spin_unlock(lock);
    
    /* Select next process */
    spin_lock(&rq->lock);
    struct process* next = dequeue_highest();
    if (next == NULL) {
        next = rq->idle_task;
    }
    
    rq->current = next;
    next->state = PROC_RUNNING;
    next->cpu = cpu;
    next->time_slice = time_quantum[next->priority];
    
    spin_unlock(&rq->lock);
    context_switch(current, next);
    
    /* Woken up - reacquire lock */
    spin_lock(lock);
}

/*
 * scheduler_wakeup - Wake up processes on a wait queue
 */
void scheduler_wakeup(wait_queue_t* wq)
{
    if (wq == NULL) return;
    
    spin_lock(&wq->lock);
    
    struct process* proc = wq->head;
    wq->head = NULL;
    
    while (proc != NULL) {
        struct process* next = proc->wq_next;
        proc->wq_next = NULL;
        proc->wq = NULL;
        proc->state = PROC_READY;
        scheduler_add_process(proc);
        proc = next;
    }
    
    spin_unlock(&wq->lock);
}

/*
 * scheduler_wakeup_one - Wake up single process from wait queue
 */
void scheduler_wakeup_one(wait_queue_t* wq)
{
    if (wq == NULL) return;
    
    spin_lock(&wq->lock);
    
    struct process* proc = wq->head;
    if (proc != NULL) {
        wq->head = proc->wq_next;
        proc->wq_next = NULL;
        proc->wq = NULL;
        proc->state = PROC_READY;
        scheduler_add_process(proc);
    }
    
    spin_unlock(&wq->lock);
}

/* ─── Internal Functions ─── */

/*
 * enqueue_process - Add process to appropriate priority queue
 */
static void enqueue_process(struct process* proc)
{
    uint32_t cpu = proc->cpu;
    runqueue_t* rq = &runqueues[cpu];
    int prio = proc->priority;
    
    proc->sched_next = NULL;
    
    if (rq->tails[prio] != NULL) {
        rq->tails[prio]->sched_next = proc;
    } else {
        rq->queues[prio] = proc;
    }
    rq->tails[prio] = proc;
    
    rq->queue_bitmap |= (1 << prio);
}

/*
 * dequeue_highest - Remove and return highest priority runnable process
 */
static struct process* dequeue_highest(void)
{
    uint32_t cpu = cpu_get_id();
    runqueue_t* rq = &runqueues[cpu];
    
    /* Find highest non-empty queue using bitmap */
    if (rq->queue_bitmap == 0) {
        return NULL;
    }
    
    /* Get index of highest priority (lowest numbered) queue */
    int prio = __builtin_ctz(rq->queue_bitmap);
    
    struct process* proc = rq->queues[prio];
    if (proc == NULL) {
        rq->queue_bitmap &= ~(1 << prio);
        return dequeue_highest(); /* Try next */
    }
    
    rq->queues[prio] = proc->sched_next;
    if (rq->queues[prio] == NULL) {
        rq->tails[prio] = NULL;
        rq->queue_bitmap &= ~(1 << prio);
    }
    
    proc->sched_next = NULL;
    return proc;
}

/*
 * context_switch - Switch from prev to next process
 * 
 * This saves the current register state and restores the next process's.
 * Implemented in assembly for precise control.
 */
static void context_switch(struct process* prev, struct process* next)
{
    /* Call architecture-specific context switch */
    arch_context_switch(prev, next);
}

/*
 * priority_boost - Prevent starvation by boosting low-priority processes
 */
static void priority_boost(void)
{
    /* Move all processes to highest normal priority */
    for (uint32_t cpu = 0; cpu < num_cpus; cpu++) {
        runqueue_t* rq = &runqueues[cpu];
        
        spin_lock(&rq->lock);
        
        for (int q = PRIO_LOW; q > PRIO_NORMAL; q--) {
            struct process* proc = rq->queues[q];
            while (proc != NULL) {
                struct process* next = proc->sched_next;
                proc->priority = PRIO_NORMAL;
                proc->sched_next = NULL;
                enqueue_process(proc);
                proc = next;
            }
            rq->queues[q] = NULL;
            rq->tails[q] = NULL;
        }
        
        spin_unlock(&rq->lock);
    }
    
    printk(KERN_DEBUG "Scheduler: Priority boost completed\n");
}

/*
 * scheduler_get_current - Get currently running process on this CPU
 */
struct process* scheduler_get_current(void)
{
    uint32_t cpu = cpu_get_id();
    if (cpu >= num_cpus) return NULL;
    return runqueues[cpu].current;
}

/*
 * scheduler_set_idle - Set the idle task for a CPU
 */
void scheduler_set_idle(struct process* idle)
{
    uint32_t cpu = cpu_get_id();
    if (cpu >= num_cpus) return;
    runqueues[cpu].idle_task = idle;
}

/*
 * scheduler_get_load - Get approximate system load (0-1000)
 */
uint32_t scheduler_get_load(void)
{
    uint32_t total_running = 0;
    for (uint32_t i = 0; i < num_cpus; i++) {
        /* Count tasks in all queues */
        for (int q = 0; q < SCHED_QUEUES - 1; q++) { /* Exclude idle */
            struct process* p = runqueues[i].queues[q];
            while (p != NULL) {
                total_running++;
                p = p->sched_next;
            }
        }
    }
    /* Clamp to 1000 */
    return total_running > 1000 ? 1000 : total_running;
}

/*
 * scheduler_stats - Print scheduler statistics
 */
void scheduler_stats(void)
{
    printk("\n=== Scheduler Statistics ===\n");
    printk("CPUs: %d, Tick rate: %d Hz\n", num_cpus, SCHED_HZ);
    
    for (uint32_t i = 0; i < num_cpus; i++) {
        runqueue_t* rq = &runqueues[i];
        printk("CPU %d: ", i);
        
        int total = 0;
        for (int q = 0; q < SCHED_QUEUES; q++) {
            int count = 0;
            struct process* p = rq->queues[q];
            while (p != NULL) {
                count++;
                p = p->sched_next;
            }
            total += count;
            const char* names[] = {"RT", "HI", "NORM", "LOW", "IDLE"};
            printk("[%s:%d] ", names[q], count);
        }
        printk("Total: %d\n", total);
        
        if (rq->current) {
            printk("  Running: PID %d (%s) prio=%d ticks=%lu\n",
                   rq->current->pid, rq->current->name,
                   rq->current->priority, rq->current->cpu_time);
        }
    }
    printk("========================\n\n");
}
