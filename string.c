/*
 * Crimson OS - String Library
 * 
 * Standard C string/memory functions for the kernel.
 * No libc dependencies - these are standalone implementations.
 */

#include <crimson/types.h>
#include <crimson/string.h>
#include <crimson/memory.h>

/* Memory operations */

void* memset(void* s, int c, size_t n)
{
    unsigned char* p = (unsigned char*)s;
    unsigned char val = (unsigned char)c;
    
    /* Use word-sized stores for large fills */
    if (n >= 8 && IS_ALIGNED((uintptr_t)p, 8)) {
        uint64_t word_val = val;
        word_val |= word_val << 8;
        word_val |= word_val << 16;
        word_val |= word_val << 32;
        
        while (n >= 8) {
            *(uint64_t*)p = word_val;
            p += 8;
            n -= 8;
        }
    }
    
    while (n--) {
        *p++ = val;
    }
    
    return s;
}

void* memcpy(void* dest, const void* src, size_t n)
{
    unsigned char* d = (unsigned char*)dest;
    const unsigned char* s = (const unsigned char*)src;
    
    /* Use word-sized copies for aligned large copies */
    if (n >= 8 && IS_ALIGNED((uintptr_t)d, 8) && IS_ALIGNED((uintptr_t)s, 8)) {
        while (n >= 8) {
            *(uint64_t*)d = *(const uint64_t*)s;
            d += 8;
            s += 8;
            n -= 8;
        }
    }
    
    while (n--) {
        *d++ = *s++;
    }
    
    return dest;
}

void* memmove(void* dest, const void* src, size_t n)
{
    unsigned char* d = (unsigned char*)dest;
    const unsigned char* s = (const unsigned char*)src;
    
    if (d < s) {
        /* Forward copy */
        while (n--) {
            *d++ = *s++;
        }
    } else if (d > s) {
        /* Backward copy (overlapping) */
        d += n;
        s += n;
        while (n--) {
            *--d = *--s;
        }
    }
    /* If d == s, nothing to do */
    
    return dest;
}

int memcmp(const void* s1, const void* s2, size_t n)
{
    const unsigned char* p1 = (const unsigned char*)s1;
    const unsigned char* p2 = (const unsigned char*)s2;
    
    while (n--) {
        if (*p1 != *p2) {
            return (int)*p1 - (int)*p2;
        }
        p1++;
        p2++;
    }
    
    return 0;
}

/* String operations */

size_t strlen(const char* s)
{
    const char* p = s;
    while (*p) p++;
    return (size_t)(p - s);
}

char* strcpy(char* dest, const char* src)
{
    char* d = dest;
    while ((*d++ = *src++) != '\0');
    return dest;
}

char* strncpy(char* dest, const char* src, size_t n)
{
    char* d = dest;
    
    while (n && (*d++ = *src++)) {
        n--;
    }
    
    /* Pad remainder with nulls */
    while (n--) {
        *d++ = '\0';
    }
    
    return dest;
}

int strcmp(const char* s1, const char* s2)
{
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return (unsigned char)*s1 - (unsigned char)*s2;
}

int strncmp(const char* s1, const char* s2, size_t n)
{
    while (n && *s1 && (*s1 == *s2)) {
        s1++;
        s2++;
        n--;
    }
    
    if (n == 0) return 0;
    return (unsigned char)*s1 - (unsigned char)*s2;
}

char* strcat(char* dest, const char* src)
{
    char* d = dest + strlen(dest);
    while ((*d++ = *src++) != '\0');
    return dest;
}

char* strchr(const char* s, int c)
{
    char ch = (char)c;
    while (*s) {
        if (*s == ch) return (char*)s;
        s++;
    }
    return (ch == '\0') ? (char*)s : NULL;
}

char* strrchr(const char* s, int c)
{
    char ch = (char)c;
    const char* last = NULL;
    
    while (*s) {
        if (*s == ch) last = s;
        s++;
    }
    
    if (ch == '\0') return (char*)s;
    return (char*)last;
}

char* strstr(const char* haystack, const char* needle)
{
    if (!*needle) return (char*)haystack;
    
    size_t needle_len = strlen(needle);
    
    while (*haystack) {
        if (*haystack == *needle) {
            if (strncmp(haystack, needle, needle_len) == 0) {
                return (char*)haystack;
            }
        }
        haystack++;
    }
    
    return NULL;
}

size_t strnlen(const char* s, size_t maxlen)
{
    size_t i = 0;
    while (i < maxlen && s[i]) i++;
    return i;
}

int atoi(const char* str)
{
    int result = 0;
    int sign = 1;

    while (*str == ' ' || *str == '\t') str++;

    if (*str == '-') { sign = -1; str++; }
    else if (*str == '+') str++;

    while (*str >= '0' && *str <= '9') {
        result = result * 10 + (*str - '0');
        str++;
    }

    return result * sign;
}

long atol(const char* str)
{
    long result = 0;
    long sign = 1;

    while (*str == ' ' || *str == '\t') str++;

    if (*str == '-') { sign = -1; str++; }
    else if (*str == '+') str++;

    while (*str >= '0' && *str <= '9') {
        result = result * 10 + (*str - '0');
        str++;
    }

    return result * sign;
}

unsigned long strtoul(const char* str, char** endptr, int base)
{
    unsigned long result = 0;

    while (*str == ' ' || *str == '\t') str++;

    while (*str) {
        int digit;
        if (*str >= '0' && *str <= '9') digit = *str - '0';
        else if (*str >= 'a' && *str <= 'z') digit = *str - 'a' + 10;
        else if (*str >= 'A' && *str <= 'Z') digit = *str - 'A' + 10;
        else break;

        if (digit >= base) break;
        result = result * base + digit;
        str++;
    }

    if (endptr) *endptr = (char*)str;
    return result;
}
