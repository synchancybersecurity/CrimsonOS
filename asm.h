/* Copyright (c) 2026 SynChanCyberSecurity LLC. All Rights Reserved. */
#ifndef _CRIMSON_ASM_H
#define _CRIMSON_ASM_H

#include <crimson/types.h>

/* Forward declaration */
struct process;

void arch_disable_interrupts(void);
void arch_enable_interrupts(void);
uint64_t arch_save_irq_disable(void);
void arch_restore_irq(uint64_t flags);
void arch_wfe(void);
void arch_hang(void);
void arch_trigger_reschedule(uint32_t cpu);
void arch_context_switch(struct process* prev, struct process* next);
void arch_jump_to_process(struct process* proc);

/* Board/CPU functions */
uint32_t cpu_get_id(void);
uint32_t cpu_get_core_count(void);
const char* board_get_name(void);

/* Debug */
void debug_stack_trace(void);

/* Kernel panic */
void kernel_panic(const char* msg);

#endif
