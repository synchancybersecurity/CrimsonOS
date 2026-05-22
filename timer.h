#ifndef _CRIMSON_TIMER_H
#define _CRIMSON_TIMER_H

#include <crimson/types.h>

#define IRQ_TIMER_VIRT  27

typedef void (*timer_callback_t)(void* arg);

void timer_init(void);
void timer_init_sched(void);
uint64_t timer_get_uptime_ms(void);
uint64_t timer_get_uptime_us(void);
uint64_t timer_get_uptime_seconds(void);
uint64_t timer_get_ticks(void);
uint32_t timer_get_frequency(void);
uint32_t timer_get_freq_mhz(void);
void timer_delay_ms(uint32_t ms);
void timer_delay_us(uint32_t us);
void timer_set_callback(int timer_id, timer_callback_t callback);
void timer_start_periodic(int timer_id, uint32_t hz);
void timer_stop(int timer_id);
int timer_create(timer_callback_t callback, void* arg);
void timer_set_oneshot(int handle, uint32_t ms);
void timer_set_periodic(int handle, uint32_t ms);
void timer_cancel(int handle);
void timer_destroy(int handle);
void timer_irq_handler(void);
uint64_t timer_cycles_to_ns(uint64_t cycles);
uint64_t timer_cycles_to_us(uint64_t cycles);
uint64_t timer_cycles_to_ms(uint64_t cycles);
uint64_t timer_perf_start(void);
uint64_t timer_perf_end(uint64_t start_cycles);

#endif
