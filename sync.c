/* Copyright (c) 2026 SynChanCyberSecurity LLC. All Rights Reserved. */
/*
 * Crimson OS - Synchronization Primitives
 * 
 * Provides the building blocks for safe concurrent access:
 * - Spinlocks: For short critical sections (interrupt-safe)
 * - Mutexes: For longer critical sections (sleep-based)
 * - Semaphores: Counting semaphores for resource management
 * - RW locks: Reader-writer locks for shared resources
 * - Barrier: Process barrier for synchronization points
 * 
 * All primitives are designed for ARM64 with proper memory ordering.
 */

#include <crimson/types.h>
#include <crimson/spinlock.h>

/* Freestanding compare-and-swap for uint32_t using ldaxr/stlxr */
static inline int cas32(volatile uint32_t* ptr, uint32_t expected, uint32_t desired)
{
    uint32_t old, tmp;
    __asm__ volatile(
        "1: ldaxr  %w[old], [%[ptr]]\n\t"
        "   cmp    %w[old], %w[exp]\n\t"
        "   b.ne   2f\n\t"
        "   stlxr  %w[tmp], %w[des], [%[ptr]]\n\t"
        "   cbnz   %w[tmp], 1b\n\t"
        "2:"
        : [old]"=&r"(old), [tmp]"=&r"(tmp), "+m"(*ptr)
        : [ptr]"r"(ptr), [exp]"r"(expected), [des]"r"(desired)
        : "cc", "memory"
    );
    return (old == expected);
}
#include <crimson/printk.h>
#include <crimson/memory.h>
#include <crimson/mutex.h>
#include <crimson/semaphore.h>
#include <crimson/scheduler.h>
#include <crimson/process.h>
#include <crimson/asm.h>

/* ─── Spinlocks ─── */

/*
 * spinlock_init - Initialize a spinlock
 */
void spinlock_init(spinlock_t* lock)
{
    if (lock == NULL) return;
    lock->lock = 0;
    lock->owner = -1;
}

/*
 * spin_lock - Acquire spinlock (disables interrupts)
 */
void spin_lock(spinlock_t* lock)
{
    if (lock == NULL) return;
    
    /* Save and disable interrupts */
    uint64_t flags = arch_save_irq_disable();
    
    /* Spin until we acquire the lock */
    while (1) {
        /* Try to acquire */
        uint32_t expected = 0;
        uint32_t desired = 1;
        
        /* LDXR/STXR atomic sequence */
        uint32_t tmp;
        __asm__ volatile (
            "1: ldaxr %w0, [%1]\n"
            "   cbnz %w0, 2f\n"
            "   stlxr %w0, %w2, [%1]\n"
            "   cbnz %w0, 1b\n"
            "   b 3f\n"
            "2: wfe\n"
            "   b 1b\n"
            "3:"
            : "=&r" (tmp)
            : "r" (&lock->lock), "r" (desired)
            : "memory"
        );
        
        if (tmp == 0) {
            /* Acquired */
            lock->owner = cpu_get_id();
            lock->irq_flags = flags;
            return;
        }
    }
}

/*
 * spin_unlock - Release spinlock (restores interrupts)
 */
void spin_unlock(spinlock_t* lock)
{
    if (lock == NULL) return;
    
    uint64_t flags = lock->irq_flags;
    
    /* Release lock with proper memory barrier */
    __asm__ volatile (
        "stlr wzr, [%0]\n"
        "sev\n"
        :
        : "r" (&lock->lock)
        : "memory"
    );
    
    lock->owner = -1;
    
    /* Restore interrupt state */
    arch_restore_irq(flags);
}

/*
 * spin_trylock - Try to acquire spinlock without blocking
 * Returns: true if acquired, false if busy
 */
bool spin_trylock(spinlock_t* lock)
{
    if (lock == NULL) return false;
    
    uint32_t tmp;
    uint32_t desired = 1;
    
    __asm__ volatile (
        "1: ldaxr %w0, [%1]\n"
        "   cbnz %w0, 2f\n"
        "   stlxr %w0, %w2, [%1]\n"
        "   cbnz %w0, 1b\n"
        "   mov %w0, #1\n"
        "   b 3f\n"
        "2: mov %w0, #0\n"
        "3:"
        : "=&r" (tmp)
        : "r" (&lock->lock), "r" (desired)
        : "memory"
    );
    
    if (tmp) {
        lock->owner = cpu_get_id();
    }
    
    return tmp != 0;
}

/*
 * spin_lock_irqsave - Save IRQ state and acquire lock
 */
uint64_t spin_lock_irqsave(spinlock_t* lock)
{
    uint64_t flags = arch_save_irq_disable();
    spin_lock(lock);
    return flags;
}

/*
 * spin_unlock_irqrestore - Release lock and restore IRQ state
 */
void spin_unlock_irqrestore(spinlock_t* lock, uint64_t flags)
{
    spin_unlock(lock);
    arch_restore_irq(flags);
}

/* ─── Mutexes ─── */

/*
 * mutex_init - Initialize a mutex
 */
void mutex_init(mutex_t* mutex)
{
    if (mutex == NULL) return;
    mutex->owner = NULL;
    mutex->locked = 0;
    spinlock_init(&mutex->wait_lock);
    mutex->waiters.head = NULL;
    mutex->waiters.lock = (spinlock_t)SPINLOCK_INIT;
}

/*
 * mutex_lock - Acquire mutex (may sleep if contended)
 */
void mutex_lock(mutex_t* mutex)
{
    if (mutex == NULL) return;
    
    struct process* current = scheduler_get_current();
    
    /* Fast path: try to acquire */
    if (!mutex->locked && cas32(&mutex->locked, 0, 1)) {
        mutex->owner = current;
        return;
    }
    
    /* Slow path: contended, sleep on wait queue */
    if (mutex->owner == current) {
        /* Deadlock detection - recursive lock attempt */
        printk(KERN_CRIT "Deadlock detected: PID %d trying to lock mutex it already owns\n",
               current->pid);
        return;
    }
    
    /* Add to waiters */
    spin_lock(&mutex->wait_lock);
    current->state = PROC_SLEEPING;
    current->wq_next = mutex->waiters.head;
    mutex->waiters.head = current;
    spin_unlock(&mutex->wait_lock);
    
    /* Yield - will be woken when mutex released */
    scheduler_yield();
}

/*
 * mutex_unlock - Release mutex
 */
void mutex_unlock(mutex_t* mutex)
{
    if (mutex == NULL) return;
    
    struct process* current = scheduler_get_current();
    
    if (mutex->owner != current) {
        printk(KERN_WARN "mutex_unlock: PID %d unlocking mutex it doesn't own\n",
               current->pid);
        return;
    }
    
    /* Check for waiters */
    spin_lock(&mutex->wait_lock);
    
    if (mutex->waiters.head != NULL) {
        /* Wake up first waiter and hand off lock */
        struct process* waiter = mutex->waiters.head;
        mutex->waiters.head = waiter->wq_next;
        waiter->wq_next = NULL;
        
        mutex->owner = waiter;
        waiter->state = PROC_READY;
        
        spin_unlock(&mutex->wait_lock);
        scheduler_add_process(waiter);
    } else {
        /* No waiters - release lock */
        mutex->owner = NULL;
        __sync_synchronize();
        mutex->locked = 0;
        spin_unlock(&mutex->wait_lock);
    }
}

/*
 * mutex_trylock - Try to acquire mutex without blocking
 */
bool mutex_trylock(mutex_t* mutex)
{
    if (mutex == NULL) return false;
    
    struct process* current = scheduler_get_current();
    
    if (!mutex->locked && cas32(&mutex->locked, 0, 1)) {
        mutex->owner = current;
        return true;
    }
    
    return false;
}

/* ─── Semaphores ─── */

/*
 * sem_init - Initialize a semaphore
 */
void sem_init(semaphore_t* sem, int32_t initial)
{
    if (sem == NULL) return;
    sem->count = initial;
    spinlock_init(&sem->lock);
    sem->waiters.head = NULL;
    sem->waiters.lock = (spinlock_t)SPINLOCK_INIT;
}

/*
 * sem_wait - Wait on semaphore (decrement, sleep if < 0)
 */
void sem_wait(semaphore_t* sem)
{
    if (sem == NULL) return;
    
    struct process* current = scheduler_get_current();
    
    spin_lock(&sem->lock);
    
    sem->count--;
    
    if (sem->count < 0) {
        /* Sleep on wait queue */
        current->state = PROC_SLEEPING;
        current->wq_next = sem->waiters.head;
        sem->waiters.head = current;
        
        spin_unlock(&sem->lock);
        scheduler_yield();
        return;
    }
    
    spin_unlock(&sem->lock);
}

/*
 * sem_signal - Signal semaphore (increment, wake waiter if any)
 */
void sem_signal(semaphore_t* sem)
{
    if (sem == NULL) return;
    
    spin_lock(&sem->lock);
    
    sem->count++;
    
    if (sem->count <= 0 && sem->waiters.head != NULL) {
        /* Wake up first waiter */
        struct process* waiter = sem->waiters.head;
        sem->waiters.head = waiter->wq_next;
        waiter->wq_next = NULL;
        waiter->state = PROC_READY;
        
        spin_unlock(&sem->lock);
        scheduler_add_process(waiter);
        return;
    }
    
    spin_unlock(&sem->lock);
}

/*
 * sem_trywait - Try to wait on semaphore without blocking
 */
bool sem_trywait(semaphore_t* sem)
{
    if (sem == NULL) return false;
    
    spin_lock(&sem->lock);
    
    if (sem->count > 0) {
        sem->count--;
        spin_unlock(&sem->lock);
        return true;
    }
    
    spin_unlock(&sem->lock);
    return false;
}

/* ─── Read-Write Locks ─── */

/*
 * rwlock_init - Initialize read-write lock
 */
void rwlock_init(rwlock_t* lock)
{
    if (lock == NULL) return;
    lock->readers = 0;
    lock->writer = 0;
    spinlock_init(&lock->lock);
}

/*
 * rwlock_read_lock - Acquire read lock
 */
void rwlock_read_lock(rwlock_t* lock)
{
    if (lock == NULL) return;
    
    while (1) {
        spin_lock(&lock->lock);
        
        if (!lock->writer) {
            lock->readers++;
            spin_unlock(&lock->lock);
            return;
        }
        
        spin_unlock(&lock->lock);
        
        /* Yield and retry */
        scheduler_yield();
    }
}

/*
 * rwlock_read_unlock - Release read lock
 */
void rwlock_read_unlock(rwlock_t* lock)
{
    if (lock == NULL) return;
    
    spin_lock(&lock->lock);
    if (lock->readers > 0) {
        lock->readers--;
    }
    spin_unlock(&lock->lock);
}

/*
 * rwlock_write_lock - Acquire write lock
 */
void rwlock_write_lock(rwlock_t* lock)
{
    if (lock == NULL) return;
    
    while (1) {
        spin_lock(&lock->lock);
        
        if (!lock->writer && lock->readers == 0) {
            lock->writer = 1;
            spin_unlock(&lock->lock);
            return;
        }
        
        spin_unlock(&lock->lock);
        
        /* Yield and retry */
        scheduler_yield();
    }
}

/*
 * rwlock_write_unlock - Release write lock
 */
void rwlock_write_unlock(rwlock_t* lock)
{
    if (lock == NULL) return;
    
    spin_lock(&lock->lock);
    lock->writer = 0;
    spin_unlock(&lock->lock);
}

/* ─── Barriers ─── */

/*
 * barrier_init - Initialize process barrier
 */
void barrier_init(barrier_t* barrier, uint32_t count)
{
    if (barrier == NULL || count == 0) return;
    barrier->target = count;
    barrier->current = 0;
    barrier->cycle = 0;
    spinlock_init(&barrier->lock);
    barrier->waiters.head = NULL;
    barrier->waiters.lock = (spinlock_t)SPINLOCK_INIT;
}

/*
 * barrier_wait - Wait at barrier
 */
void barrier_wait(barrier_t* barrier)
{
    if (barrier == NULL) return;
    
    struct process* current = scheduler_get_current();
    
    spin_lock(&barrier->lock);
    
    uint32_t cycle = barrier->cycle;
    barrier->current++;
    
    if (barrier->current >= barrier->target) {
        /* Last arrival - release all */
        barrier->current = 0;
        barrier->cycle++;
        
        /* Wake all waiters */
        struct process* p = barrier->waiters.head;
        barrier->waiters.head = NULL;
        spin_unlock(&barrier->lock);
        
        while (p != NULL) {
            struct process* next = p->wq_next;
            p->wq_next = NULL;
            p->state = PROC_READY;
            scheduler_add_process(p);
            p = next;
        }
        
        return;
    }
    
    /* Not last - sleep */
    current->state = PROC_SLEEPING;
    current->wq_next = barrier->waiters.head;
    barrier->waiters.head = current;
    
    spin_unlock(&barrier->lock);
    scheduler_yield();
}
