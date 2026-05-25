/* Copyright (c) 2026 SynChanCyberSecurity LLC. All Rights Reserved. */
#ifndef _CRIMSON_UART_H
#define _CRIMSON_UART_H

#include <crimson/types.h>

void uart_early_init(void);
void uart_init(void);
void uart_putc(char c);
int uart_getc(void);
char uart_getc_blocking(void);
void uart_puts(const char* str);
void uart_write(const char* buf, size_t len);
size_t uart_read(char* buf, size_t len);
bool uart_tx_ready(void);
bool uart_rx_ready(void);
void uart_flush(void);
void uart_irq_handler(void);
void uart_set_baud(uint32_t baud);
void uart_debug_dump(void);

#endif
