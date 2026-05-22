/*
 * Crimson OS - UART Serial Driver
 * 
 * Pl011 UART driver for ARM64 platforms.
 * Provides the primary serial console for kernel output.
 * 
 * Supported: Raspberry Pi 4, QEMU virt, most ARM64 dev boards
 * Baud rate: 115200 (configurable)
 * Data: 8 bits, no parity, 1 stop bit
 * 
 * This is the first driver initialized — printk depends on it.
 */

#include <crimson/types.h>
#include <crimson/uart.h>
#include <crimson/gpio.h>
#include <crimson/memory.h>
#include <crimson/spinlock.h>
#include <crimson/printk.h>

/* PL011 UART Register Offsets */
#define UART_DR         0x00    /* Data Register */
#define UART_RSRECR     0x04    /* Receive Status / Error Clear */
#define UART_FR         0x18    /* Flag Register */
#define UART_ILPR       0x20    /* IrDA Low-Power Counter */
#define UART_IBRD       0x24    /* Integer Baud Rate Divisor */
#define UART_FBRD       0x28    /* Fractional Baud Rate Divisor */
#define UART_LCRH       0x2C    /* Line Control Register */
#define UART_CR         0x30    /* Control Register */
#define UART_IFLS       0x34    /* Interrupt FIFO Level Select */
#define UART_IMSC       0x38    /* Interrupt Mask Set/Clear */
#define UART_RIS        0x3C    /* Raw Interrupt Status */
#define UART_MIS        0x40    /* Masked Interrupt Status */
#define UART_ICR        0x44    /* Interrupt Clear Register */
#define UART_DMACR      0x48    /* DMA Control Register */

/* Flag Register bits */
#define FR_CTS          (1 << 0)    /* Clear to Send */
#define FR_DSR          (1 << 1)    /* Data Set Ready */
#define FR_DCD          (1 << 2)    /* Data Carrier Detect */
#define FR_BUSY         (1 << 3)    /* UART Busy */
#define FR_RXFE         (1 << 4)    /* Receive FIFO Empty */
#define FR_TXFF         (1 << 5)    /* Transmit FIFO Full */
#define FR_RXFF         (1 << 6)    /* Receive FIFO Full */
#define FR_TXFE         (1 << 7)    /* Transmit FIFO Empty */
#define FR_RI           (1 << 8)    /* Ring Indicator */

/* Line Control Register bits */
#define LCRH_BRK        (1 << 0)    /* Send Break */
#define LCRH_PEN        (1 << 1)    /* Parity Enable */
#define LCRH_EPS        (1 << 2)    /* Even Parity Select */
#define LCRH_STP2       (1 << 3)    /* Two Stop Bits */
#define LCRH_FEN        (1 << 4)    /* Enable FIFOs */
#define LCRH_WLEN_5     (0 << 5)    /* 5 bit words */
#define LCRH_WLEN_6     (1 << 5)    /* 6 bit words */
#define LCRH_WLEN_7     (2 << 5)    /* 7 bit words */
#define LCRH_WLEN_8     (3 << 5)    /* 8 bit words */
#define LCRH_SPS        (1 << 7)    /* Stick Parity Select */

/* Control Register bits */
#define CR_UARTEN       (1 << 0)    /* UART Enable */
#define CR_LBE          (1 << 7)    /* Loopback Enable */
#define CR_TXE          (1 << 8)    /* Transmit Enable */
#define CR_RXE          (1 << 9)    /* Receive Enable */
#define CR_RTS          (1 << 11)   /* Request to Send */
#define CR_RTSEN        (1 << 14)   /* CTS Hardware Flow Control */
#define CR_CTSEN        (1 << 15)   /* RTS Hardware Flow Control */

/* Interrupt bits */
#define IMSC_RXIM       (1 << 4)    /* Receive interrupt */
#define IMSC_TXIM       (1 << 5)    /* Transmit interrupt */
#define IMSC_RTIM       (1 << 6)    /* Receive timeout interrupt */

/* Platform-specific base addresses */
#ifdef BOARD_RPI4
  #define UART_BASE       0xFE201000    /* RPi4 UART0 */
  #define UART_CLK        48000000      /* 48MHz UART clock */
#elif defined(BOARD_RPI3)
  #define UART_BASE       0x3F201000    /* RPi3 UART0 */
  #define UART_CLK        48000000
#elif defined(BOARD_QEMU)
  #define UART_BASE       0x09000000    /* QEMU virt machine */
  #define UART_CLK        24000000
#else
  #define UART_BASE       0x09000000    /* Default */
  #define UART_CLK        24000000
#endif

#define TARGET_BAUD     115200

/* Spinlock for UART access */
static spinlock_t uart_lock = SPINLOCK_INIT;
static volatile uint8_t* uart_base = NULL;
static int uart_initialized = 0;

/* Static register access */
#define UART_REG(off)   (*(volatile uint32_t*)((uintptr_t)uart_base + (off)))

/*
 * uart_early_init - Minimal UART init for early printk
 * Called before memory management is available
 */
void uart_early_init(void)
{
    uart_base = (volatile uint8_t*)UART_BASE;
    
    /* Disable UART during config */
    UART_REG(UART_CR) = 0;
    
    /* Calculate baud rate divisors */
    uint32_t baud_div = UART_CLK / (16 * TARGET_BAUD);
    uint32_t frac_div = ((UART_CLK % (16 * TARGET_BAUD)) * 64 + TARGET_BAUD / 2) / (16 * TARGET_BAUD);
    
    UART_REG(UART_IBRD) = baud_div;
    UART_REG(UART_FBRD) = frac_div;
    
    /* 8N1, FIFOs enabled */
    UART_REG(UART_LCRH) = LCRH_WLEN_8 | LCRH_FEN;
    
    /* Enable UART, TX, RX */
    UART_REG(UART_CR) = CR_UARTEN | CR_TXE | CR_RXE;
    
    uart_initialized = 1;
}

/*
 * uart_init - Full UART initialization
 */
void uart_init(void)
{
    if (!uart_initialized) {
        uart_early_init();
    }
    
    /* Clear any pending interrupts */
    UART_REG(UART_ICR) = 0x7FF;
    
    /* Set FIFO levels */
    UART_REG(UART_IFLS) = (2 << 3) | 2;  /* RX 1/2 full, TX 1/8 full */
    
    /* Enable receive interrupt */
    UART_REG(UART_IMSC) = IMSC_RXIM | IMSC_RTIM;
    
    printk(KERN_DEBUG "UART: PL011 at 0x%p, %d baud\n", uart_base, TARGET_BAUD);
}

/*
 * uart_putc - Send a single character (polling)
 */
void uart_putc(char c)
{
    if (!uart_initialized) return;
    
    /* Wait for TX FIFO not full */
    while (UART_REG(UART_FR) & FR_TXFF) {
        __asm__ volatile("nop");
    }
    
    UART_REG(UART_DR) = c;
    
    /* CRLF handling */
    if (c == '\n') {
        uart_putc('\r');
    }
}

/*
 * uart_getc - Receive a single character (non-blocking)
 * Returns: character, or -1 if none available
 */
int uart_getc(void)
{
    if (!uart_initialized) return -1;
    
    /* Check if RX FIFO has data */
    if (UART_REG(UART_FR) & FR_RXFE) {
        return -1;
    }
    
    return UART_REG(UART_DR) & 0xFF;
}

/*
 * uart_getc_blocking - Receive a character, blocking
 */
char uart_getc_blocking(void)
{
    int c;
    while ((c = uart_getc()) < 0) {
        __asm__ volatile("wfe");
    }
    return (char)c;
}

/*
 * uart_puts - Send a null-terminated string
 */
void uart_puts(const char* str)
{
    if (!str) return;
    
    while (*str) {
        uart_putc(*str++);
    }
}

/*
 * uart_write - Write buffer of data
 */
void uart_write(const char* buf, size_t len)
{
    if (!buf) return;
    
    for (size_t i = 0; i < len; i++) {
        uart_putc(buf[i]);
    }
}

/*
 * uart_read - Read data into buffer (non-blocking)
 * Returns: number of bytes read
 */
size_t uart_read(char* buf, size_t len)
{
    if (!buf || len == 0) return 0;
    
    size_t i = 0;
    int c;
    while (i < len && (c = uart_getc()) >= 0) {
        buf[i++] = (char)c;
    }
    return i;
}

/*
 * uart_tx_ready - Check if transmitter is ready
 */
bool uart_tx_ready(void)
{
    return !(UART_REG(UART_FR) & FR_TXFF);
}

/*
 * uart_rx_ready - Check if receiver has data
 */
bool uart_rx_ready(void)
{
    return !(UART_REG(UART_FR) & FR_RXFE);
}

/*
 * uart_flush - Flush TX FIFO (wait for all data sent)
 */
void uart_flush(void)
{
    /* Wait for TX FIFO empty and not busy */
    while (UART_REG(UART_FR) & FR_BUSY) {
        __asm__ volatile("nop");
    }
}

/*
 * uart_irq_handler - UART interrupt handler
 */
void uart_irq_handler(void)
{
    uint32_t status = UART_REG(UART_MIS);
    
    if (status & IMSC_RXIM) {
        /* Receive interrupt - read all available data */
        while (!(UART_REG(UART_FR) & FR_RXFE)) {
            char c = UART_REG(UART_DR) & 0xFF;
            /* TODO: Add to input buffer for shell */
            (void)c;
        }
    }
    
    if (status & IMSC_TXIM) {
        /* Transmit interrupt - feed more data if available */
    }
    
    /* Clear all interrupts */
    UART_REG(UART_ICR) = status;
}

/*
 * uart_set_baud - Change baud rate
 */
void uart_set_baud(uint32_t baud)
{
    if (baud == 0) return;
    
    /* Disable UART */
    uint32_t cr = UART_REG(UART_CR);
    UART_REG(UART_CR) = 0;
    
    /* Calculate new divisors */
    uint32_t baud_div = UART_CLK / (16 * baud);
    uint32_t frac_div = ((UART_CLK % (16 * baud)) * 64 + baud / 2) / (16 * baud);
    
    UART_REG(UART_IBRD) = baud_div;
    UART_REG(UART_FBRD) = frac_div;
    
    /* Re-enable */
    UART_REG(UART_CR) = cr;
}

/*
 * uart_debug_dump - Dump UART registers for debugging
 */
void uart_debug_dump(void)
{
    printk("UART Registers:\n");
    printk("  CR:   0x%08x\n", UART_REG(UART_CR));
    printk("  LCRH: 0x%08x\n", UART_REG(UART_LCRH));
    printk("  FR:   0x%08x\n", UART_REG(UART_FR));
    printk("  IBRD: 0x%08x\n", UART_REG(UART_IBRD));
    printk("  FBRD: 0x%08x\n", UART_REG(UART_FBRD));
}
