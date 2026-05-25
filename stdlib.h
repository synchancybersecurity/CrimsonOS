/* Copyright (c) 2026 SynChanCyberSecurity LLC. All Rights Reserved. */
/*
 * Crimson OS - stdlib.h
 * Standard library functions header
 */
#ifndef _CRIMSON_STDLIB_H
#define _CRIMSON_STDLIB_H

#include <crimson/types.h>

int     atoi(const char* str);
long    strtol(const char* str, char** endptr, int base);
unsigned long strtoul(const char* str, char** endptr, int base);
char*   itoa(int value, char* buf, int base);

#endif
