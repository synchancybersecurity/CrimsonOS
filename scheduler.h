/* Copyright (c) 2026 SynChanCyberSecurity LLC. All Rights Reserved. */
#ifndef _CRIMSON_SCHEDULER_H
#define _CRIMSON_SCHEDULER_H

#include <crimson/types.h>
#include <crimson/process.h>
#include <crimson/spinlock.h>

#define SCHED_HZ        100

#define TIMER_ID_SCHED  0

/* timer_callback_t defined in timer.h */

void scheduler_init(void);
void scheduler_start(void) __attribute__((noreturn));
void scheduler_tick(void);
void scheduler_yield(void);
void scheduler_add_process(struct process* proc);
void scheduler_remove_process(struct process* proc);
void scheduler_sleep_on(wait_queue_t* wq, spinlock_t* lock);
void scheduler_wakeup(wait_queue_t* wq);
void scheduler_wakeup_one(wait_queue_t* wq);
struct process* scheduler_get_current(void);
void scheduler_set_idle(struct process* idle);
uint32_t scheduler_get_load(void);
void scheduler_stats(void);

#endif
