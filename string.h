#ifndef _CRIMSON_STRING_H
#define _CRIMSON_STRING_H

#include <crimson/types.h>

void* memset(void* s, int c, size_t n);
void* memcpy(void* dest, const void* src, size_t n);
void* memmove(void* dest, const void* src, size_t n);
int memcmp(const void* s1, const void* s2, size_t n);

size_t strlen(const char* s);
char* strcpy(char* dest, const char* src);
char* strncpy(char* dest, const char* src, size_t n);
int strcmp(const char* s1, const char* s2);
int strncmp(const char* s1, const char* s2, size_t n);
char* strcat(char* dest, const char* src);
char* strchr(const char* s, int c);
char* strrchr(const char* s, int c);
char* strstr(const char* haystack, const char* needle);

size_t strnlen(const char* s, size_t maxlen);

int atoi(const char* str);
long atol(const char* str);
unsigned long strtoul(const char* str, char** endptr, int base);
char* itoa(int value, char* str, int base);

int snprintf(char* str, size_t size, const char* format, ...);

#endif
