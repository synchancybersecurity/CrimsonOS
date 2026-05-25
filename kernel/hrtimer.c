/* Copyright (c) 2026 SynChanCyberSecurity LLC. All Rights Reserved. */
#include <crimson/hrtimer.h>
#include <crimson/sched.h>
#include <crimson/uart.h>
#include <crimson/memory.h>

#define HRTIMER_MAX_CALLBACKS   16
#define HRTIMER_MIN_INTERVAL_US 10
#define HRTIMER_MAX_INTERVAL_US 1000000

static hrtimer_entry_t g_timers[HRTIMER_MAX_CALLBACKS];
static uint32_t g_cntfrq;

static inline uint64_t cntpct_to_us(uint64_t cntpct) {
    return (cntpct * 1000000ULL) / g_cntfrq;
}

static inline uint64_t read_cntpct_us(void) {
    uint64_t cntpct;
    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(cntpct));
    return cntpct_to_us(cntpct);
}

void hrtimer_init(void) {
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(g_cntfrq));
    if (g_cntfrq == 0) g_cntfrq = 19200000;
    for (int i = 0; i < HRTIMER_MAX_CALLBACKS; i++) g_timers[i].active = 0;
    uart_puts("[hrtimer] init\n");
}

void hrtimer_tick(uint64_t now_us) {
    for (int i = 0; i < HRTIMER_MAX_CALLBACKS; i++) {
        if (g_timers[i].active && now_us >= g_timers[i].next_fire_us) {
            g_timers[i].next_fire_us += g_timers[i].interval_us;
            if (g_timers[i].cb) g_timers[i].cb(g_timers[i].arg);
            struct task *t = sched_get_task(g_timers[i].pid);
            if (t) t->pending_signal = 1;
        }
    }
}

int hrtimer_register(uint8_t pid, uint64_t interval_us, hrtimer_callback_t cb, void *arg) {
    if (interval_us < HRTIMER_MIN_INTERVAL_US || interval_us > HRTIMER_MAX_INTERVAL_US) return -1;
    for (int i = 0; i < HRTIMER_MAX_CALLBACKS; i++) {
        if (!g_timers[i].active) {
            g_timers[i].interval_us = interval_us;
            g_timers[i].next_fire_us = read_cntpct_us() + interval_us;
            g_timers[i].cb = cb; g_timers[i].arg = arg;
            g_timers[i].pid = pid; g_timers[i].active = 1;
            return i;
        }
    }
    return -1;
}

int hrtimer_unregister(uint8_t pid) {
    for (int i = 0; i < HRTIMER_MAX_CALLBACKS; i++) {
        if (g_timers[i].active && g_timers[i].pid == pid) {
            g_timers[i].active = 0; return 0;
        }
    }
    return -1;
}

int sys_hrtimer_create(uint64_t interval_us, void *user_handler) {
    (void)user_handler;
    struct task *t = sched_current_task();
    if (!t) return -1;
    return hrtimer_register(t->pid, interval_us, NULL, NULL);
}

int sys_hrtimer_destroy(void) {
    struct task *t = sched_current_task();
    if (!t) return -1;
    return hrtimer_unregister(t->pid);
}
