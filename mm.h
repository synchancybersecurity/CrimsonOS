/* Copyright (c) 2026 SynChanCyberSecurity LLC. All Rights Reserved. */
/*
 * Crimson OS - mm.h
 * Memory management convenience header
 * Wraps memory.h for files that include <crimson/mm.h>
 */
#ifndef _CRIMSON_MM_H
#define _CRIMSON_MM_H

#include <crimson/memory.h>
#include <crimson/spinlock.h>

/* PMM */
void     pmm_init(uintptr_t start, size_t size);
size_t pmm_get_free_pages(void);
void*    pmm_alloc_page(void);
void     pmm_free_page(void* page);

/* Kernel heap */
void     kmalloc_init(void);
void*    kmalloc(size_t size);
void     kfree(void* ptr);

/* VMM */
#ifndef pgd_t
typedef uint64_t pgd_t;
#endif

#define VMM_FLAG_USER   (1 << 0)
#define VMM_FLAG_WRITE  (1 << 1)
#define VMM_FLAG_NX     (1 << 2)

int      vmm_map_page(pgd_t* pgd, uintptr_t vaddr, uintptr_t paddr, uint64_t flags);

/* Utility */
void     kmemset(void* dst, int val, size_t n);
void     kmemcpy(void* dst, const void* src, size_t n);
int      kmemcmp(const void* a, const void* b, size_t n);

#endif
