#ifndef _CRIMSON_MUTEX_H
#define _CRIMSON_MUTEX_H

#include <crimson/types.h>
#include <crimson/memory.h>
#include <crimson/spinlock.h>

struct process;

typedef struct {
    struct process* owner;
    volatile int locked;
    spinlock_t wait_lock;
    wait_queue_t waiters;
} mutex_t;

void mutex_init(mutex_t* mutex);
void mutex_lock(mutex_t* mutex);
void mutex_unlock(mutex_t* mutex);
bool mutex_trylock(mutex_t* mutex);

#endif
