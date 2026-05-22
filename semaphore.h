#ifndef _CRIMSON_SEMAPHORE_H
#define _CRIMSON_SEMAPHORE_H

#include <crimson/types.h>
#include <crimson/memory.h>
#include <crimson/spinlock.h>

typedef struct {
    volatile int32_t count;
    spinlock_t lock;
    wait_queue_t waiters;
} semaphore_t;

typedef struct {
    uint32_t target;
    uint32_t current;
    uint32_t cycle;
    spinlock_t lock;
    wait_queue_t waiters;
} barrier_t;

typedef struct {
    volatile uint32_t readers;
    volatile int writer;
    spinlock_t lock;
} rwlock_t;

void sem_init(semaphore_t* sem, int32_t initial);
void sem_wait(semaphore_t* sem);
void sem_signal(semaphore_t* sem);
bool sem_trywait(semaphore_t* sem);

void rwlock_init(rwlock_t* lock);
void rwlock_read_lock(rwlock_t* lock);
void rwlock_read_unlock(rwlock_t* lock);
void rwlock_write_lock(rwlock_t* lock);
void rwlock_write_unlock(rwlock_t* lock);

void barrier_init(barrier_t* barrier, uint32_t count);
void barrier_wait(barrier_t* barrier);

#endif
