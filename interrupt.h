/* Copyright (c) 2026 SynChanCyberSecurity LLC. All Rights Reserved. */
#ifndef _CRIMSON_INTERRUPT_H
#define _CRIMSON_INTERRUPT_H

#include <crimson/types.h>

typedef void (*irq_handler_t)(uint32_t irq, void* data);

void gic_init(void);
void interrupt_init(void);
void interrupt_register(uint32_t irq, irq_handler_t handler, const char* name);
void interrupt_register_with_data(uint32_t irq, irq_handler_t handler, void* data, const char* name);
void interrupt_enable(uint32_t irq);
void interrupt_disable(uint32_t irq);
void interrupt_set_priority(uint32_t irq, uint8_t priority);
void interrupt_set_target(uint32_t irq, uint8_t cpu_mask);
void interrupt_trigger_sgi(uint32_t sgi, uint8_t target_mask);

void irq_handler(void);
void sync_exception_handler(uint64_t esr, uint64_t elr, uint64_t spsr);

#endif
