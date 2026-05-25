/* Copyright (c) 2026 SynChanCyberSecurity LLC. All Rights Reserved. */
/*
 * Crimson OS - Boot stubs header
 * Forward declarations matching real header signatures exactly
 */
#ifndef _CRIMSON_STUBS_H
#define _CRIMSON_STUBS_H

#include <crimson/types.h>

/* CPU */
unsigned long cpu_get_core_count(void);

/* RNG & Crypto */
void rng_init(void);
void crypto_init(void);
void keystore_init(void);

/* Debug */
void debug_stack_trace(void);

/* Arch */
void arch_disable_interrupts(void);
void arch_enable_interrupts(void);
void arch_wfe(void);
void arch_hang(void);

/* UART */
void uart_early_init(void);
void uart_putc(char c);
void uart_puts(const char* s);
void uart_init(void);

/* GPIO */
void gpio_init(void);

/* Shell */
void shell_run(void);

/* Display */
void display_swap_buffers(void);

/* Board */
const char* board_get_name(void);

#endif
