/*
 * Crimson OS - Standard Library
 * 
 * atoi, strtoul, itoa and other utility functions.
 */

#include <crimson/types.h>
#include <crimson/string.h>

int atoi(const char* str)
{
    int result = 0;
    int sign = 1;
    
    /* Skip whitespace */
    while (*str == ' ' || *str == '\t') str++;
    
    /* Handle sign */
    if (*str == '-') {
        sign = -1;
        str++;
    } else if (*str == '+') {
        str++;
    }
    
    /* Parse digits */
    while (*str >= '0' && *str <= '9') {
        result = result * 10 + (*str - '0');
        str++;
    }
    
    return result * sign;
}

long atol(const char* str)
{
    long result = 0;
    int sign = 1;
    
    while (*str == ' ' || *str == '\t') str++;
    
    if (*str == '-') {
        sign = -1;
        str++;
    } else if (*str == '+') {
        str++;
    }
    
    while (*str >= '0' && *str <= '9') {
        result = result * 10 + (*str - '0');
        str++;
    }
    
    return result * sign;
}

unsigned long strtoul(const char* str, char** endptr, int base)
{
    unsigned long result = 0;
    
    /* Skip whitespace */
    while (*str == ' ' || *str == '\t') str++;
    
    /* Determine base */
    if (base == 0) {
        if (*str == '0') {
            str++;
            if (*str == 'x' || *str == 'X') {
                base = 16;
                str++;
            } else {
                base = 8;
            }
        } else {
            base = 10;
        }
    } else if (base == 16) {
        if (*str == '0' && (*(str + 1) == 'x' || *(str + 1) == 'X')) {
            str += 2;
        }
    }
    
    /* Parse digits */
    while (1) {
        int digit;
        
        if (*str >= '0' && *str <= '9') {
            digit = *str - '0';
        } else if (*str >= 'a' && *str <= 'z') {
            digit = *str - 'a' + 10;
        } else if (*str >= 'A' && *str <= 'Z') {
            digit = *str - 'A' + 10;
        } else {
            break;
        }
        
        if (digit >= base) break;
        
        result = result * base + digit;
        str++;
    }
    
    if (endptr) {
        *endptr = (char*)str;
    }
    
    return result;
}

char* itoa(int value, char* str, int base)
{
    char* rc = str;
    char* ptr = str;
    char* low = str;
    
    /* Validate base */
    if (base < 2 || base > 36) {
        *str = '\0';
        return str;
    }
    
    /* Handle sign for base 10 */
    if (value < 0 && base == 10) {
        *ptr++ = '-';
        low++;
    }
    
    /* Convert to string (in reverse) */
    unsigned int uvalue = (value < 0) ? -value : value;
    
    do {
        *ptr++ = "0123456789abcdefghijklmnopqrstuvwxyz"[uvalue % base];
        uvalue /= base;
    } while (uvalue);
    
    *ptr-- = '\0';
    
    /* Reverse the string */
    while (low < ptr) {
        char tmp = *low;
        *low++ = *ptr;
        *ptr-- = tmp;
    }
    
    return rc;
}
