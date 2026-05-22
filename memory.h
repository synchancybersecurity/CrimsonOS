/*
 * Crimson OS - Memory Management Header
 */

#ifndef _CRIMSON_MEMORY_H
#define _CRIMSON_MEMORY_H

#include <crimson/types.h>

/* Page size - 4KB */
#define PAGE_SIZE       4096
#define PAGE_SHIFT      12
#define PAGE_MASK       (~(PAGE_SIZE - 1))

/* Alignment */
#define ALIGN_UP(x, a)      (((x) + (a) - 1) & ~((a) - 1))
#define ALIGN_DOWN(x, a)    ((x) & ~((a) - 1))
#define IS_ALIGNED(x, a)    (((x) & ((a) - 1)) == 0)

/* Page table entry flags */
#define PT_VALID        (1 << 0)
#define PT_TABLE        (1 << 1)
#define PT_BLOCK        (0 << 1)
#define PT_PAGE         (1 << 1)
#define PT_XN           (1ULL << 54)
#define PT_AP_RW        (0 << 6)
#define PT_AP_RO        (2 << 6)
#define PT_AP_RW_USER   (1 << 6)
#define PT_AP_RO_USER   (3 << 6)
#define PT_SH           (3 << 8)
#define PT_AF           (1 << 10)
#define PT_NG           (1 << 11)
#define PT_ADDR_MASK    0x0000FFFFFFFFF000ULL

/* Page table types */
typedef uint64_t pte_t;
typedef uint64_t pmd_t;
typedef uint64_t pud_t;
typedef uint64_t pgd_t;

/* Full spinlock definition needed to embed in wait_queue_t */
#include <crimson/spinlock.h>

/* Heap block header */
typedef struct heap_block {
    size_t size;
    int used;
    uint32_t magic;
    struct heap_block* next;
} heap_block_t;

#define HEAP_MAGIC      0xCE150007

/* Wait queue */
typedef struct {
    struct process* head;
    spinlock_t lock;
} wait_queue_t;

/* Physical Memory Manager */
void pmm_init(uintptr_t base, size_t size);
uintptr_t pmm_alloc(void);
uintptr_t pmm_alloc_n(size_t n);
void pmm_free(uintptr_t paddr);
void pmm_mark_used(uintptr_t paddr);
size_t pmm_get_free_pages(void);

extern size_t total_pages;

/* Kernel Heap */
void kmalloc_init(void);
void* kmalloc(size_t size);
void* kcalloc(size_t nmemb, size_t size);
void* krealloc(void* ptr, size_t new_size);
void kfree(void* ptr);

/* Virtual Memory Manager */
void vmm_init(pgd_t* pgd);
int vmm_map(pgd_t* pgd, uintptr_t vaddr, uintptr_t paddr, uint32_t flags);
void vmm_unmap(pgd_t* pgd, uintptr_t vaddr);
uintptr_t vmm_alloc_pages(pgd_t* pgd, uintptr_t vaddr, size_t npages, uint32_t flags);
uintptr_t vmm_get_phys(pgd_t* pgd, uintptr_t vaddr);
void vmm_free_all(pgd_t* pgd);

/* String functions (kernel libc) */
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
int atoi(const char* str);
long atol(const char* str);
unsigned long strtoul(const char* str, char** endptr, int base);
char* itoa(int value, char* str, int base);

#endif
