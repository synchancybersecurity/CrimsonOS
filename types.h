/* Copyright (c) 2026 SynChanCyberSecurity LLC. All Rights Reserved. */
/*
 * Crimson OS - Core Type Definitions
 * All fundamental types used across the entire kernel
 */

#ifndef _CRIMSON_TYPES_H
#define _CRIMSON_TYPES_H

/* Fixed-width integer types */
typedef signed char        int8_t;
typedef unsigned char      uint8_t;
typedef short              int16_t;
typedef unsigned short     uint16_t;
typedef int                int32_t;
typedef unsigned int       uint32_t;
typedef long long          int64_t;
typedef unsigned long long uint64_t;

/* Size types */
typedef unsigned long      size_t;
typedef long               ssize_t;
typedef long               intptr_t;
typedef unsigned long      uintptr_t;

/* Boolean */
#if !defined(__cplusplus) && !defined(__bool_true_false_are_defined)
#if __STDC_VERSION__ < 202311L
typedef int                bool;
#define true  1
#define false 0
#define __bool_true_false_are_defined 1
#endif
#endif
#define true  1
#define false 0

/* NULL */
#ifndef NULL
#define NULL ((void*)0)
#endif

/* Volatile memory-mapped I/O types */
typedef volatile uint32_t  reg32_t;
typedef volatile uint64_t  reg64_t;

/* Physical/virtual address types */
typedef uintptr_t          phys_addr_t;
typedef uintptr_t          virt_addr_t;

/* Process ID */
typedef int                pid_t;

/* File offsets */
typedef long               off_t;

/* User/group IDs */
typedef uint32_t           uid_t;
typedef uint32_t           gid_t;

/* File permissions */
typedef uint32_t           mode_t;

/* Result type */
typedef int                result_t;
#define RESULT_OK     0
#define RESULT_ERROR -1

/* Status codes */
typedef enum {
    STATUS_OK = 0,
    STATUS_ERROR,
    STATUS_NOMEM,
    STATUS_INVAL,
    STATUS_NOTFOUND,
    STATUS_TIMEOUT,
    STATUS_BUSY,
    STATUS_UNIMPL,
    STATUS_PERM,
} status_t;

#endif
