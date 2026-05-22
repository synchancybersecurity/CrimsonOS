#ifndef _CRIMSON_PRINTK_H
#define _CRIMSON_PRINTK_H

#include <crimson/types.h>

#define LOGLEVEL_EMERG   0
#define LOGLEVEL_ALERT   1
#define LOGLEVEL_CRIT    2
#define LOGLEVEL_ERROR   3
#define LOGLEVEL_WARN    4
#define LOGLEVEL_NOTICE  5
#define LOGLEVEL_INFO    6
#define LOGLEVEL_DEBUG   7

#define KERN_EMERG   "<0>"
#define KERN_ALERT   "<1>"
#define KERN_CRIT    "<2>"
#define KERN_ERROR   "<3>"
#define KERN_WARN    "<4>"
#define KERN_NOTICE  "<5>"
#define KERN_INFO    "<6>"
#define KERN_DEBUG   "<7>"

typedef __builtin_va_list va_list;
#define va_start(v,l)  __builtin_va_start(v,l)
#define va_end(v)      __builtin_va_end(v)
#define va_arg(v,t)    __builtin_va_arg(v,t)

/* ANSI color codes for terminal output */
#define CLR_RED     "\x1B[31m"
#define CLR_GREEN   "\x1B[32m"
#define CLR_YELLOW  "\x1B[33m"
#define CLR_BLUE    "\x1B[34m"
#define CLR_MAGENTA "\x1B[35m"
#define CLR_CYAN    "\x1B[36m"
#define CLR_WHITE   "\x1B[37m"
#define CLR_RESET   "\x1B[0m"

/* Convenience logging macros */
#define INFO  KERN_INFO "[INFO] "
#define WARN  KERN_WARN "[WARN] "
#define ERR   KERN_ERROR "[ERR] "
#define OK    KERN_INFO "[OK] "

void printk_init(void (*sink)(const char*));
void printk(const char* fmt, ...);
void vprintk(const char* fmt, va_list args);
void printk_set_level(int level);
int printk_get_level(void);
size_t dmesg(char* buf, size_t size);
void dmesg_clear(void);
void printk_hexdump(const void* addr, size_t len);

int vsnprintf(char* str, size_t size, const char* fmt, va_list args);

#endif
