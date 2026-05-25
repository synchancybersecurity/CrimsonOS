/* Copyright (c) 2026 SynChanCyberSecurity LLC. All Rights Reserved. */
/*
 * Crimson OS - Timer Driver
 * 
 * ARM Generic Timer (CNTVCT/CNTV_CVAL) driver.
 * Provides system timing, scheduling ticks, and high-resolution
 * timestamps for profiling and timeouts.
 * 
 * Features:
 * - System uptime tracking (microsecond resolution)
 * - Periodic timer for scheduler (100Hz default)
 * - One-shot timers for delays and timeouts
 * - High-resolution performance counters
 * 
 * Uses: ARM Generic Timer (built into all ARMv8 cores)
 */

#include <crimson/types.h>
#include <crimson/timer.h>
#include <crimson/interrupt.h>
#include <crimson/printk.h>
#include <crimson/asm.h>
#include <crimson/spinlock.h>

/* ARM Generic Timer registers (system register access) */
#define CNTFRQ_EL0      "cntfrq_el0"    /* Counter frequency */
#define CNTPCT_EL0      "cntpct_el0"    /* Physical counter value */
#define CNTVCT_EL0      "cntvct_el0"    /* Virtual counter value */
#define CNTV_CTL_EL0    "cntv_ctl_el0"  /* Virtual timer control */
#define CNTV_CVAL_EL0   "cntv_cval_el0" /* Virtual timer compare */
#define CNTV_TVAL_EL0   "cntv_tval_el0" /* Virtual timer value */

#define CNTP_CTL_EL0    "cntp_ctl_el0"  /* Physical timer control */
#define CNTP_TVAL_EL0   "cntp_tval_el0" /* Physical timer value */

/* Timer control bits */
#define CTL_ENABLE      (1 << 0)
#define CTL_IMASK       (1 << 1)
#define CTL_ISTATUS     (1 << 2)

/* Maximum number of software timers */
#define MAX_SOFT_TIMERS 16

/* Software timer structure */
typedef struct {
    bool active;
    uint64_t expiry;        /* Expiration time in ticks */
    uint32_t interval;      /* For periodic timers (0 = one-shot) */
    timer_callback_t callback;
    void* arg;
} soft_timer_t;

/* Timer state */
static uint32_t timer_frequency = 0;        /* Counter frequency in Hz */
static uint64_t timer_ticks = 0;            /* Total ticks since boot */
static uint64_t boot_time_us = 0;           /* Boot time reference */

/* Software timers */
static soft_timer_t soft_timers[MAX_SOFT_TIMERS];
static spinlock_t timer_lock = SPINLOCK_INIT;

/* Scheduler timer callback */
static timer_callback_t sched_callback = NULL;

#define TIMER_ID_SCHED  0   /* Scheduler heartbeat timer ID */

/* Forward declarations */
uint64_t timer_get_ticks(void);
static void timer_update_soft_timers(void);

/*
 * timer_init - Initialize timer subsystem
 */
void timer_init(void)
{
    /* Read counter frequency */
    uint64_t freq;
    __asm__ volatile("mrs %0, " CNTFRQ_EL0 : "=r" (freq));
    timer_frequency = (uint32_t)freq;
    
    /* Read initial counter value */
    uint64_t count;
    __asm__ volatile("mrs %0, " CNTPCT_EL0 : "=r" (count));
    boot_time_us = (count * 1000000ULL) / timer_frequency;
    
    /* Clear software timers */
    for (int i = 0; i < MAX_SOFT_TIMERS; i++) {
        soft_timers[i].active = false;
    }
    
    printk(KERN_DEBUG "Timer: ARM Generic Timer, freq=%u Hz\n", timer_frequency);
}

/* Wrapper matching irq_handler_t signature */
static void timer_irq_dispatch(uint32_t irq, void* data)
{
    (void)irq; (void)data;
    timer_irq_handler();
}

/*
 * timer_init_sched - Initialize scheduler timer
 */
void timer_init_sched(void)
{
    /* Register IRQ handler for ARM virtual timer (IRQ 27 on virt machine) */
    interrupt_register(IRQ_TIMER_VIRT, timer_irq_dispatch, "timer");

    /* Enable virtual timer with interrupt */
    uint32_t ctl = CTL_ENABLE;
    __asm__ volatile("msr " CNTV_CTL_EL0 ", %0" :: "r" (ctl));

    /* Set initial tick interval */
    uint32_t ticks_per_tick = timer_frequency / 100; /* 100Hz */
    __asm__ volatile("msr " CNTV_TVAL_EL0 ", %0" :: "r" (ticks_per_tick));

    /* Enable timer interrupt in GIC */
    interrupt_enable(IRQ_TIMER_VIRT);
}

/*
 * timer_get_uptime_ms - Get system uptime in milliseconds
 */
uint64_t timer_get_uptime_ms(void)
{
    uint64_t count;
    __asm__ volatile("mrs %0, " CNTVCT_EL0 : "=r" (count));
    
    return (count * 1000ULL) / timer_frequency;
}

/*
 * timer_get_uptime_us - Get system uptime in microseconds
 */
uint64_t timer_get_uptime_us(void)
{
    uint64_t count;
    __asm__ volatile("mrs %0, " CNTVCT_EL0 : "=r" (count));
    
    return (count * 1000000ULL) / timer_frequency;
}

/*
 * timer_get_ticks - Get raw timer count
 */
uint64_t timer_get_ticks(void)
{
    uint64_t count;
    __asm__ volatile("mrs %0, " CNTVCT_EL0 : "=r" (count));
    return count;
}

/*
 * timer_get_uptime_seconds - Get system uptime in seconds
 */
uint64_t timer_get_uptime_seconds(void)
{
    return timer_get_uptime_ms() / 1000;
}

/*
 * timer_get_frequency - Get timer frequency in Hz
 */
uint32_t timer_get_frequency(void)
{
    return timer_frequency;
}

/*
 * timer_get_freq_mhz - Get timer frequency in MHz
 */
uint32_t timer_get_freq_mhz(void)
{
    return timer_frequency / 1000000;
}

/*
 * timer_delay_ms - Busy-wait delay in milliseconds
 * NOTE: This is a busy-wait. Use timer_sleep_ms for yielding delays.
 */
void timer_delay_ms(uint32_t ms)
{
    uint64_t start = timer_get_ticks();
    uint64_t ticks_to_wait = ((uint64_t)ms * timer_frequency) / 1000;
    
    while ((timer_get_ticks() - start) < ticks_to_wait) {
        __asm__ volatile("wfi");
    }
}

/*
 * timer_delay_us - Busy-wait delay in microseconds
 */
void timer_delay_us(uint32_t us)
{
    uint64_t start = timer_get_ticks();
    uint64_t ticks_to_wait = ((uint64_t)us * timer_frequency) / 1000000;
    
    while ((timer_get_ticks() - start) < ticks_to_wait) {
        __asm__ volatile("nop");
    }
}

/*
 * timer_set_callback - Set scheduler tick callback
 */
void timer_set_callback(int timer_id, timer_callback_t callback)
{
    if (timer_id == TIMER_ID_SCHED) {
        sched_callback = callback;
    }
}

/*
 * timer_start_periodic - Start periodic timer
 * @timer_id: Timer identifier
 * @hz: Frequency in Hz
 */
void timer_start_periodic(int timer_id, uint32_t hz)
{
    if (timer_id == TIMER_ID_SCHED) {
        uint32_t ticks = timer_frequency / hz;
        __asm__ volatile("msr " CNTV_TVAL_EL0 ", %0" :: "r" (ticks));
    }
}

/*
 * timer_stop - Stop a timer
 */
void timer_stop(int timer_id)
{
    if (timer_id == TIMER_ID_SCHED) {
        uint32_t ctl = 0;
        __asm__ volatile("msr " CNTV_CTL_EL0 ", %0" :: "r" (ctl));
    }
}

/* ─── Software Timers ─── */

/*
 * timer_create - Create a software timer
 * Returns: timer handle, or -1 on error
 */
int timer_create(timer_callback_t callback, void* arg)
{
    spin_lock(&timer_lock);
    
    for (int i = 0; i < MAX_SOFT_TIMERS; i++) {
        if (!soft_timers[i].active) {
            soft_timers[i].active = true;
            soft_timers[i].callback = callback;
            soft_timers[i].arg = arg;
            soft_timers[i].interval = 0;
            spin_unlock(&timer_lock);
            return i;
        }
    }
    
    spin_unlock(&timer_lock);
    printk(KERN_WARN "timer_create: no free timer slots\n");
    return -1;
}

/*
 * timer_set_oneshot - Configure one-shot timer
 * @handle: Timer handle
 * @ms: Milliseconds until expiration
 */
void timer_set_oneshot(int handle, uint32_t ms)
{
    if (handle < 0 || handle >= MAX_SOFT_TIMERS) return;
    
    spin_lock(&timer_lock);
    
    if (soft_timers[handle].active) {
        soft_timers[handle].expiry = timer_get_uptime_ms() + ms;
        soft_timers[handle].interval = 0;
    }
    
    spin_unlock(&timer_lock);
}

/*
 * timer_set_periodic - Configure periodic timer
 * @handle: Timer handle
 * @ms: Milliseconds between firings
 */
void timer_set_periodic(int handle, uint32_t ms)
{
    if (handle < 0 || handle >= MAX_SOFT_TIMERS) return;
    
    spin_lock(&timer_lock);
    
    if (soft_timers[handle].active) {
        soft_timers[handle].expiry = timer_get_uptime_ms() + ms;
        soft_timers[handle].interval = ms;
    }
    
    spin_unlock(&timer_lock);
}

/*
 * timer_cancel - Cancel a timer
 */
void timer_cancel(int handle)
{
    if (handle < 0 || handle >= MAX_SOFT_TIMERS) return;
    
    spin_lock(&timer_lock);
    soft_timers[handle].interval = 0;
    soft_timers[handle].expiry = 0;
    spin_unlock(&timer_lock);
}

/*
 * timer_destroy - Destroy a timer
 */
void timer_destroy(int handle)
{
    if (handle < 0 || handle >= MAX_SOFT_TIMERS) return;
    
    spin_lock(&timer_lock);
    soft_timers[handle].active = false;
    spin_unlock(&timer_lock);
}

/* ─── Interrupt Handler ─── */

/*
 * timer_irq_handler - Called on timer interrupt
 */
void timer_irq_handler(void)
{
    timer_ticks++;
    
    /* Acknowledge interrupt by setting next tick */
    uint32_t ticks_per_tick = timer_frequency / 100;
    __asm__ volatile("msr " CNTV_TVAL_EL0 ", %0" :: "r" (ticks_per_tick));
    
    /* Call scheduler tick callback */
    if (sched_callback) {
        sched_callback(NULL);
    }
    
    /* Check software timers */
    timer_update_soft_timers();
}

/*
 * timer_update_soft_timers - Check and fire expired software timers
 */
static void timer_update_soft_timers(void)
{
    uint64_t now = timer_get_uptime_ms();
    
    for (int i = 0; i < MAX_SOFT_TIMERS; i++) {
        if (!soft_timers[i].active) continue;
        if (soft_timers[i].expiry == 0) continue;
        
        if (now >= soft_timers[i].expiry) {
            /* Timer expired */
            soft_timer_t* t = &soft_timers[i];
            
            if (t->interval > 0) {
                /* Periodic - set next expiry */
                t->expiry = now + t->interval;
            } else {
                /* One-shot - deactivate */
                t->expiry = 0;
            }
            
            /* Call callback */
            if (t->callback) {
                t->callback(t->arg);
            }
        }
    }
}

/* ─── Profiling / Performance ─── */

/*
 * timer_cycles_to_ns - Convert timer cycles to nanoseconds
 */
uint64_t timer_cycles_to_ns(uint64_t cycles)
{
    return (cycles * 1000000000ULL) / timer_frequency;
}

/*
 * timer_cycles_to_us - Convert timer cycles to microseconds
 */
uint64_t timer_cycles_to_us(uint64_t cycles)
{
    return (cycles * 1000000ULL) / timer_frequency;
}

/*
 * timer_cycles_to_ms - Convert timer cycles to milliseconds
 */
uint64_t timer_cycles_to_ms(uint64_t cycles)
{
    return (cycles * 1000ULL) / timer_frequency;
}

/*
 * timer_perf_start - Start performance measurement
 */
uint64_t timer_perf_start(void)
{
    uint64_t count;
    __asm__ volatile("mrs %0, " CNTVCT_EL0 : "=r" (count));
    return count;
}

/*
 * timer_perf_end - End performance measurement, return elapsed ns
 */
uint64_t timer_perf_end(uint64_t start_cycles)
{
    uint64_t end;
    __asm__ volatile("mrs %0, " CNTVCT_EL0 : "=r" (end));
    return timer_cycles_to_ns(end - start_cycles);
}
