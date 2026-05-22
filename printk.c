/*
 * Crimson OS - Kernel Print / Logging
 * 
 * printk() - The kernel's voice. Every debug message, error, and status
 * report flows through here. Supports log levels, formatting, and
 * output to multiple sinks (UART, framebuffer, network logging).
 * 
 * Log levels control what gets printed based on compile-time config:
 *   KERN_CRIT  - System is unusable
 *   KERN_ERROR - Critical errors
 *   KERN_WARN  - Warnings
 *   KERN_INFO  - Informational
 *   KERN_DEBUG - Debug messages
 * 
 * All logs are also stored in a ring buffer for later retrieval
 * by the Crimson Shell 'dmesg' command.
 */

#include <crimson/types.h>
#include <crimson/printk.h>
#include <crimson/spinlock.h>
#include <crimson/driver.h>

/* Ring buffer for kernel log */
#define LOG_BUF_SIZE    16384       /* 16KB kernel log buffer */
#define LOG_BUF_MASK    (LOG_BUF_SIZE - 1)

static char log_buffer[LOG_BUF_SIZE];
static volatile size_t log_head = 0;   /* Write position */
static volatile size_t log_tail = 0;   /* Read position */
static spinlock_t log_lock = SPINLOCK_INIT;

/* Current log level threshold */
static int console_loglevel = LOGLEVEL_INFO;

/* Output sink function pointer */
static void (*output_sink)(const char* str) = NULL;

/* Number formatting buffer */
static char fmt_buf[64];

/* Forward declarations */
static void store_to_logbuf(const char* str);
static void fmt_putc(char c);
static void fmt_puts(const char* s);
static void fmt_uint(uint64_t num, int base, int width, char pad, bool upper);
static void fmt_int(int64_t num, int width, char pad);
static void fmt_ptr(void* p);

/*
 * printk_init - Initialize the logging subsystem
 * @sink: Function to call for actual output (usually uart_puts)
 */
void printk_init(void (*sink)(const char*))
{
    output_sink = sink;
    log_head = 0;
    log_tail = 0;
    
    printk(KERN_INFO "Crimson OS Kernel Log initialized\n");
}

/*
 * printk - Kernel formatted print
 * @fmt: Format string with optional KERN_ prefix
 * 
 * Format specifiers:
 *   %% - literal %
 *   %c - character
 *   %s - string
 *   %d - signed decimal
 *   %u - unsigned decimal
 *   %x - hex (lowercase)
 *   %X - hex (uppercase)
 *   %p - pointer
 *   %l[dux] - long variants
 *   %[0][width][l]type - with padding
 */
void printk(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vprintk(fmt, args);
    va_end(args);
}

/*
 * vprintk - va_list version of printk
 */
void vprintk(const char* fmt, va_list args)
{
    /* Check log level prefix */
    int level = LOGLEVEL_INFO;
    const char* p = fmt;
    
    if (fmt[0] == '<' && fmt[2] == '>') {
        level = fmt[1] - '0';
        p = fmt + 3;
    }
    
    /* Filter by log level */
    if (level > console_loglevel) {
        return;
    }
    
    spin_lock(&log_lock);
    
    while (*p) {
        if (*p == '%' && *(p + 1)) {
            p++;
            
            /* Parse flags */
            bool left_align = false;
            char pad_char = ' ';
            int width = 0;
            bool is_long = false;
            
            if (*p == '-') {
                left_align = true;
                p++;
            }
            if (*p == '0') {
                pad_char = '0';
                p++;
            }
            while (*p >= '0' && *p <= '9') {
                width = width * 10 + (*p - '0');
                p++;
            }
            if (*p == 'l') {
                is_long = true;
                p++;
            }
            
            /* Format specifier */
            switch (*p) {
                case 'c': {
                    char c = (char)va_arg(args, int);
                    fmt_putc(c);
                    break;
                }
                
                case 's': {
                    const char* s = va_arg(args, const char*);
                    if (s == NULL) s = "(null)";
                    fmt_puts(s);
                    break;
                }
                
                case 'd':
                case 'i': {
                    int64_t num;
                    if (is_long) {
                        num = va_arg(args, long);
                    } else {
                        num = va_arg(args, int);
                    }
                    fmt_int(num, width, pad_char);
                    break;
                }
                
                case 'u': {
                    uint64_t num;
                    if (is_long) {
                        num = va_arg(args, unsigned long);
                    } else {
                        num = va_arg(args, unsigned int);
                    }
                    fmt_uint(num, 10, width, pad_char, false);
                    break;
                }
                
                case 'x':
                case 'X': {
                    uint64_t num;
                    if (is_long) {
                        num = va_arg(args, unsigned long);
                    } else {
                        num = va_arg(args, unsigned int);
                    }
                    fmt_puts("0x");
                    fmt_uint(num, 16, width, pad_char, *p == 'X');
                    break;
                }
                
                case 'p': {
                    void* ptr = va_arg(args, void*);
                    fmt_ptr(ptr);
                    break;
                }
                
                case '%':
                    fmt_putc('%');
                    break;
                
                default:
                    fmt_putc('%');
                    fmt_putc(*p);
                    break;
            }
        } else {
            fmt_putc(*p);
        }
        p++;
    }
    
    spin_unlock(&log_lock);
}

/*
 * printk_set_level - Set console log level
 */
void printk_set_level(int level)
{
    if (level >= LOGLEVEL_EMERG && level <= LOGLEVEL_DEBUG) {
        console_loglevel = level;
    }
}

/*
 * printk_get_level - Get current console log level
 */
int printk_get_level(void)
{
    return console_loglevel;
}

/*
 * dmesg - Copy kernel log buffer to user buffer
 * Returns: Number of bytes copied
 */
size_t dmesg(char* buf, size_t size)
{
    if (buf == NULL || size == 0) return 0;
    
    spin_lock(&log_lock);
    
    size_t available;
    if (log_head >= log_tail) {
        available = log_head - log_tail;
    } else {
        available = LOG_BUF_SIZE - log_tail + log_head;
    }
    
    size_t to_copy = (available < size - 1) ? available : size - 1;
    
    for (size_t i = 0; i < to_copy; i++) {
        buf[i] = log_buffer[(log_tail + i) & LOG_BUF_MASK];
    }
    buf[to_copy] = '\0';
    
    spin_unlock(&log_lock);
    
    return to_copy;
}

/*
 * dmesg_clear - Clear the kernel log buffer
 */
void dmesg_clear(void)
{
    spin_lock(&log_lock);
    log_head = 0;
    log_tail = 0;
    spin_unlock(&log_lock);
}

/* ─── Internal Formatting ─── */

static void fmt_putc(char c)
{
    char str[2] = {c, '\0'};
    
    /* Store to ring buffer */
    log_buffer[log_head & LOG_BUF_MASK] = c;
    log_head++;
    
    /* Advance tail if buffer full (overwrite oldest) */
    if ((log_head - log_tail) > LOG_BUF_SIZE) {
        log_tail = log_head - LOG_BUF_SIZE;
    }
    
    /* Send to output sink */
    if (output_sink) {
        output_sink(str);
    }
}

static void fmt_puts(const char* s)
{
    while (*s) {
        fmt_putc(*s++);
    }
}

static void fmt_uint(uint64_t num, int base, int width, char pad, bool upper)
{
    const char* digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    
    if (num == 0) {
        if (width > 1) {
            for (int i = 0; i < width - 1; i++) {
                fmt_putc(pad);
            }
        }
        fmt_putc('0');
        return;
    }
    
    char buf[32];
    int i = 0;
    
    while (num > 0) {
        buf[i++] = digits[num % base];
        num /= base;
    }
    
    /* Pad */
    int digits_count = i;
    if (width > digits_count) {
        for (int j = 0; j < width - digits_count; j++) {
            fmt_putc(pad);
        }
    }
    
    /* Output in reverse */
    while (i > 0) {
        fmt_putc(buf[--i]);
    }
}

static void fmt_int(int64_t num, int width, char pad)
{
    if (num < 0) {
        fmt_putc('-');
        if (width > 0) width--;
        fmt_uint((uint64_t)(-num), 10, width, pad, false);
    } else {
        fmt_uint((uint64_t)num, 10, width, pad, false);
    }
}

static void fmt_ptr(void* p)
{
    fmt_puts("0x");
    fmt_uint((uint64_t)(uintptr_t)p, 16, 16, '0', false);
}

/*
 * printk_hexdump - Hexdump a memory region
 */
void printk_hexdump(const void* addr, size_t len)
{
    const uint8_t* data = (const uint8_t*)addr;
    
    for (size_t i = 0; i < len; i += 16) {
        printk("%p  ", (void*)(data + i));
        
        /* Hex bytes */
        for (size_t j = 0; j < 16; j++) {
            if (i + j < len) {
                printk("%02x ", data[i + j]);
            } else {
                printk("   ");
            }
            if (j == 7) printk(" ");
        }
        
        printk(" |");
        
        /* ASCII representation */
        for (size_t j = 0; j < 16 && (i + j) < len; j++) {
            char c = data[i + j];
            if (c >= 32 && c < 127) {
                printk("%c", c);
            } else {
                printk(".");
            }
        }
        
        printk("|\n");
    }
}

/* ─── snprintf / vsnprintf ─── */

typedef struct {
    char*   buf;
    size_t  size;
    size_t  pos;
} fmt_buf_t;

static void snprintf_putc(fmt_buf_t* fb, char c)
{
    if (fb->pos < fb->size - 1) {
        fb->buf[fb->pos++] = c;
        fb->buf[fb->pos] = '\0';
    }
}

static void snprintf_puts(fmt_buf_t* fb, const char* s)
{
    while (*s) {
        snprintf_putc(fb, *s++);
    }
}

static void snprintf_uint(fmt_buf_t* fb, uint64_t num, int base, int width, char pad, int upper)
{
    const char* digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";

    if (num == 0) {
        if (width > 1) {
            for (int i = 0; i < width - 1; i++)
                snprintf_putc(fb, pad);
        }
        snprintf_putc(fb, '0');
        return;
    }

    char buf[32];
    int i = 0;
    while (num > 0) {
        buf[i++] = digits[num % base];
        num /= base;
    }

    int digits_count = i;
    if (width > digits_count) {
        for (int j = 0; j < width - digits_count; j++)
            snprintf_putc(fb, pad);
    }

    while (i > 0)
        snprintf_putc(fb, buf[--i]);
}

static void snprintf_int(fmt_buf_t* fb, int64_t num, int width, char pad)
{
    if (num < 0) {
        snprintf_putc(fb, '-');
        if (width > 0) width--;
        snprintf_uint(fb, (uint64_t)(-num), 10, width, pad, 0);
    } else {
        snprintf_uint(fb, (uint64_t)num, 10, width, pad, 0);
    }
}

static void snprintf_ptr(fmt_buf_t* fb, void* p)
{
    snprintf_puts(fb, "0x");
    snprintf_uint(fb, (uint64_t)(uintptr_t)p, 16, 16, '0', 0);
}

int vsnprintf(char* str, size_t size, const char* fmt, va_list args)
{
    if (!str || size == 0) return 0;

    fmt_buf_t fb = { str, size, 0 };
    str[0] = '\0';

    const char* p = fmt;
    while (*p) {
        if (*p == '%' && *(p + 1)) {
            p++;

            bool left_align = false;
            char pad_char = ' ';
            int width = 0;
            bool is_long = false;

            if (*p == '-') {
                left_align = true;
                p++;
            }
            if (*p == '0') {
                pad_char = '0';
                p++;
            }
            while (*p >= '0' && *p <= '9') {
                width = width * 10 + (*p - '0');
                p++;
            }
            if (*p == 'l') {
                is_long = true;
                p++;
            }

            switch (*p) {
                case 'c': {
                    char c = (char)va_arg(args, int);
                    snprintf_putc(&fb, c);
                    break;
                }
                case 's': {
                    const char* s = va_arg(args, const char*);
                    if (s == NULL) s = "(null)";
                    snprintf_puts(&fb, s);
                    break;
                }
                case 'd':
                case 'i': {
                    int64_t num = is_long ? va_arg(args, long) : va_arg(args, int);
                    snprintf_int(&fb, num, width, pad_char);
                    break;
                }
                case 'u': {
                    uint64_t num = is_long ? va_arg(args, unsigned long) : va_arg(args, unsigned int);
                    snprintf_uint(&fb, num, 10, width, pad_char, 0);
                    break;
                }
                case 'x':
                case 'X': {
                    uint64_t num = is_long ? va_arg(args, unsigned long) : va_arg(args, unsigned int);
                    snprintf_puts(&fb, "0x");
                    snprintf_uint(&fb, num, 16, width, pad_char, *p == 'X');
                    break;
                }
                case 'p': {
                    void* ptr = va_arg(args, void*);
                    snprintf_ptr(&fb, ptr);
                    break;
                }
                case '%':
                    snprintf_putc(&fb, '%');
                    break;
                default:
                    snprintf_putc(&fb, '%');
                    snprintf_putc(&fb, *p);
                    break;
            }
        } else {
            snprintf_putc(&fb, *p);
        }
        p++;
    }

    if (fb.pos < size)
        str[fb.pos] = '\0';
    else
        str[size - 1] = '\0';

    return (int)fb.pos;
}

int snprintf(char* str, size_t size, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    int ret = vsnprintf(str, size, fmt, args);
    va_end(args);
    return ret;
}
