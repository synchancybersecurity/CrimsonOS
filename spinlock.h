/* Copyright (c) 2026 SynChanCyberSecurity LLC. All Rights Reserved. */
#ifndef _CRIMSON_SPINLOCK_H
#define _CRIMSON_SPINLOCK_H

#include <crimson/types.h>

typedef struct spinlock {
    volatile uint32_t lock;
    int owner;
    uint64_t irq_flags;
} spinlock_t;

#define SPINLOCK_INIT   { .lock = 0, .owner = -1, .irq_flags = 0 }

void spinlock_init(spinlock_t* lock);
void spin_lock(spinlock_t* lock);
void spin_unlock(spinlock_t* lock);
bool spin_trylock(spinlock_t* lock);
uint64_t spin_lock_irqsave(spinlock_t* lock);
void spin_unlock_irqrestore(spinlock_t* lock, uint64_t flags);

#endif
