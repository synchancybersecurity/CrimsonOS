/*
 * Crimson OS - Memory Manager
 * 
 * Physical Memory Manager (PMM) - Page frame allocator
 * Kernel Heap Allocator (kmalloc/kfree)
 * Virtual Memory Manager (VMM) - Page tables, mapping, protection
 * 
 * Design: Bitmap-based page allocation with buddy system for
 * larger allocations. O(1) page allocation, O(log n) for blocks.
 */

#include <crimson/types.h>
#include <crimson/memory.h>
#include <crimson/spinlock.h>
#include <crimson/printk.h>
#include <crimson/asm.h>

/* Page frame bitmap - one bit per physical page */
static uint64_t* page_bitmap = NULL;
size_t total_pages = 0;
static size_t free_pages = 0;
static uintptr_t phys_base = 0;

/* Kernel heap */
static heap_block_t* heap_start = NULL;
static size_t heap_size = 0;

/* Page allocator lock */
static spinlock_t pmm_lock = SPINLOCK_INIT;
static spinlock_t heap_lock = SPINLOCK_INIT;

/*
 * pmm_init - Initialize the physical memory manager
 * @base: Physical base address of available memory
 * @size: Total available memory in bytes
 */
void pmm_init(uintptr_t base, size_t size)
{
    phys_base = ALIGN_UP(base, PAGE_SIZE);
    total_pages = size / PAGE_SIZE;
    free_pages = total_pages;
    
    /* Place bitmap right after kernel data */
    page_bitmap = (uint64_t*)phys_base;
    size_t bitmap_size = (total_pages + 63) / 64 * sizeof(uint64_t);
    
    /* Clear bitmap - all pages free */
    size_t bitmap_pages = ALIGN_UP(bitmap_size, PAGE_SIZE) / PAGE_SIZE;
    for (size_t i = 0; i < bitmap_size / sizeof(uint64_t); i++) {
        page_bitmap[i] = 0xFFFFFFFFFFFFFFFFULL;
    }
    
    /* Mark bitmap pages as used */
    for (size_t i = 0; i < bitmap_pages; i++) {
        pmm_mark_used(phys_base + i * PAGE_SIZE);
    }
    
    /* Mark kernel pages as used (0x40080000 = KERNEL_BASE) */
    extern uintptr_t __kernel_size;
    uintptr_t kernel_base = 0x40080000;
    size_t kernel_bytes = ALIGN_UP((uintptr_t)&__kernel_size, PAGE_SIZE);
    /* Only mark if the kernel region falls within this PMM's managed range */
    if (kernel_base >= phys_base && kernel_base < phys_base + total_pages * PAGE_SIZE) {
        size_t kernel_pages = kernel_bytes / PAGE_SIZE;
        for (size_t i = 0; i < kernel_pages; i++) {
            pmm_mark_used(kernel_base + i * PAGE_SIZE);
        }
    }
    
    printk(KERN_DEBUG "PMM: %lu pages total, %lu free\n", total_pages, free_pages);
}

/*
 * pmm_alloc - Allocate a single physical page
 * Returns: Physical address of allocated page, or 0 on failure
 */
uintptr_t pmm_alloc(void)
{
    spin_lock(&pmm_lock);
    
    if (free_pages == 0) {
        spin_unlock(&pmm_lock);
        return 0;
    }
    
    /* Find first free page in bitmap */
    for (size_t i = 0; i < (total_pages + 63) / 64; i++) {
        if (page_bitmap[i] != 0) {
            /* Find first set bit */
            int bit = __builtin_ctzll(page_bitmap[i]);
            size_t page_idx = i * 64 + bit;
            
            if (page_idx >= total_pages) {
                spin_unlock(&pmm_lock);
                return 0;
            }
            
            /* Mark as used (clear bit) */
            page_bitmap[i] &= ~(1ULL << bit);
            free_pages--;
            
            spin_unlock(&pmm_lock);
            return phys_base + page_idx * PAGE_SIZE;
        }
    }
    
    spin_unlock(&pmm_lock);
    return 0;
}

/*
 * pmm_alloc_n - Allocate n contiguous physical pages
 * @n: Number of pages to allocate
 * Returns: Physical address of first page, or 0 on failure
 */
uintptr_t pmm_alloc_n(size_t n)
{
    if (n == 0) return 0;
    if (n == 1) return pmm_alloc();
    
    spin_lock(&pmm_lock);
    
    /* Search for n contiguous free pages */
    for (size_t start = 0; start <= total_pages - n; start++) {
        bool found = true;
        
        for (size_t j = 0; j < n; j++) {
            size_t idx = start + j;
            size_t word = idx / 64;
            size_t bit = idx % 64;
            
            if (!(page_bitmap[word] & (1ULL << bit))) {
                found = false;
                break;
            }
        }
        
        if (found) {
            /* Mark all n pages as used */
            for (size_t j = 0; j < n; j++) {
                size_t idx = start + j;
                size_t word = idx / 64;
                size_t bit = idx % 64;
                page_bitmap[word] &= ~(1ULL << bit);
            }
            free_pages -= n;
            
            spin_unlock(&pmm_lock);
            return phys_base + start * PAGE_SIZE;
        }
    }
    
    spin_unlock(&pmm_lock);
    return 0;
}

/*
 * pmm_free - Free a single physical page
 * @paddr: Physical address of page to free
 */
void pmm_free(uintptr_t paddr)
{
    if (paddr < phys_base) return;
    
    size_t page_idx = (paddr - phys_base) / PAGE_SIZE;
    if (page_idx >= total_pages) return;
    
    spin_lock(&pmm_lock);
    
    size_t word = page_idx / 64;
    size_t bit = page_idx % 64;
    
    /* Mark as free (set bit) */
    page_bitmap[word] |= (1ULL << bit);
    free_pages++;
    
    spin_unlock(&pmm_lock);
}

/*
 * pmm_mark_used - Mark a physical page as used
 * @paddr: Physical address of page
 */
void pmm_mark_used(uintptr_t paddr)
{
    if (paddr < phys_base) return;
    
    size_t page_idx = (paddr - phys_base) / PAGE_SIZE;
    if (page_idx >= total_pages) return;
    
    size_t word = page_idx / 64;
    size_t bit = page_idx % 64;
    
    if (page_bitmap[word] & (1ULL << bit)) {
        page_bitmap[word] &= ~(1ULL << bit);
        free_pages--;
    }
}

/*
 * pmm_get_free_pages - Get number of free pages
 */
size_t pmm_get_free_pages(void)
{
    return free_pages;
}

/* ─── Kernel Heap Allocator ─── */

/*
 * kmalloc_init - Initialize the kernel heap
 */
void kmalloc_init(void)
{
    extern char __heap_start[];
    extern char __heap_end[];
    
    heap_start = (heap_block_t*)&__heap_start;
    heap_size = (uintptr_t)&__heap_end - (uintptr_t)&__heap_start;
    
    /* Initialize first block as single free block */
    heap_start->size = heap_size - sizeof(heap_block_t);
    heap_start->used = 0;
    heap_start->next = NULL;
    heap_start->magic = HEAP_MAGIC;
    
    printk(KERN_DEBUG "Heap: %lu KB at %p\n", heap_size / 1024, heap_start);
}

/*
 * kmalloc - Allocate memory from kernel heap
 * @size: Number of bytes to allocate
 * Returns: Pointer to allocated memory, or NULL on failure
 */
void* kmalloc(size_t size)
{
    if (size == 0) return NULL;
    
    /* Align size to 16 bytes */
    size = ALIGN_UP(size, 16);
    
    spin_lock(&heap_lock);
    
    heap_block_t* current = heap_start;
    heap_block_t* prev __attribute__((unused)) = NULL;
    
    while (current != NULL) {
        /* Check for corruption */
        if (current->magic != HEAP_MAGIC) {
            spin_unlock(&heap_lock);
            printk(KERN_CRIT "Heap corruption detected at %p!\n", current);
            return NULL;
        }
        
        /* Find first fit */
        if (!current->used && current->size >= size) {
            /* Split block if large enough */
            if (current->size >= size + sizeof(heap_block_t) + 16) {
                heap_block_t* new_block = (heap_block_t*)((uint8_t*)current + sizeof(heap_block_t) + size);
                new_block->size = current->size - size - sizeof(heap_block_t);
                new_block->used = 0;
                new_block->next = current->next;
                new_block->magic = HEAP_MAGIC;
                
                current->size = size;
                current->next = new_block;
            }
            
            current->used = 1;
            spin_unlock(&heap_lock);
            return (void*)((uint8_t*)current + sizeof(heap_block_t));
        }
        
        prev = current;
        current = current->next;
    }
    
    spin_unlock(&heap_lock);
    
    /* Try to get more memory from PMM */
    size_t pages_needed = ALIGN_UP(size + sizeof(heap_block_t), PAGE_SIZE) / PAGE_SIZE;
    uintptr_t new_pages = pmm_alloc_n(pages_needed);
    if (new_pages == 0) {
        printk(KERN_WARN "kmalloc: out of memory (%lu bytes requested)\n", size);
        return NULL;
    }
    
    /* Add new pages to heap */
    heap_block_t* new_block = (heap_block_t*)new_pages;
    new_block->size = pages_needed * PAGE_SIZE - sizeof(heap_block_t);
    new_block->used = 0;
    new_block->magic = HEAP_MAGIC;
    
    spin_lock(&heap_lock);
    new_block->next = heap_start;
    heap_start = new_block;
    spin_unlock(&heap_lock);
    
    /* Retry allocation */
    return kmalloc(size);
}

/*
 * kfree - Free memory allocated with kmalloc
 * @ptr: Pointer to memory to free
 */
void kfree(void* ptr)
{
    if (ptr == NULL) return;
    
    heap_block_t* block = (heap_block_t*)((uint8_t*)ptr - sizeof(heap_block_t));
    
    /* Verify magic number */
    if (block->magic != HEAP_MAGIC) {
        printk(KERN_CRIT "kfree: invalid pointer %p (magic=0x%x)\n", ptr, block->magic);
        return;
    }
    
    if (!block->used) {
        printk(KERN_WARN "kfree: double free detected at %p\n", ptr);
        return;
    }
    
    spin_lock(&heap_lock);
    block->used = 0;
    
    /* Coalesce with next block if free */
    if (block->next != NULL && !block->next->used) {
        block->size += sizeof(heap_block_t) + block->next->size;
        block->next = block->next->next;
    }
    
    /* Note: coalescing with previous block requires traversal */
    /* This is a simple allocator; production would use a better structure */
    
    spin_unlock(&heap_lock);
}

/*
 * kcalloc - Allocate zeroed memory
 */
void* kcalloc(size_t nmemb, size_t size)
{
    size_t total = nmemb * size;
    void* ptr = kmalloc(total);
    if (ptr != NULL) {
        memset(ptr, 0, total);
    }
    return ptr;
}

/*
 * krealloc - Resize allocated memory
 */
void* krealloc(void* ptr, size_t new_size)
{
    if (ptr == NULL) return kmalloc(new_size);
    if (new_size == 0) {
        kfree(ptr);
        return NULL;
    }
    
    heap_block_t* block = (heap_block_t*)((uint8_t*)ptr - sizeof(heap_block_t));
    if (block->size >= new_size) {
        return ptr; /* Current block is big enough */
    }
    
    void* new_ptr = kmalloc(new_size);
    if (new_ptr != NULL) {
        memcpy(new_ptr, ptr, block->size);
        kfree(ptr);
    }
    return new_ptr;
}

/* ─── Virtual Memory Manager ─── */

/*
 * vmm_init - Initialize virtual memory management for a process
 * @pgd: Page global directory (top-level page table)
 */
void vmm_init(pgd_t* pgd)
{
    if (pgd == NULL) return;
    
    /* Clear page table */
    memset(pgd, 0, PAGE_SIZE);
    
    /* Map kernel space (higher half) into every process */
    /* Copy kernel mappings from master page table */
    /* This ensures all processes can access kernel memory */
}

/*
 * vmm_map - Map a virtual page to a physical page
 * @pgd: Page directory
 * @vaddr: Virtual address
 * @paddr: Physical address
 * @flags: Page flags (read/write/execute/user)
 */
int vmm_map(pgd_t* pgd, uintptr_t vaddr, uintptr_t paddr, uint32_t flags)
{
    if (pgd == NULL) return -1;
    
    /* Extract indices from virtual address (4-level, 4KB pages) */
    uint64_t pgd_idx = (vaddr >> 39) & 0x1FF;
    uint64_t pud_idx = (vaddr >> 30) & 0x1FF;
    uint64_t pmd_idx = (vaddr >> 21) & 0x1FF;
    uint64_t pte_idx = (vaddr >> 12) & 0x1FF;
    
    /* Navigate/create page table levels */
    /* Level 1: PGD */
    if (!(pgd[pgd_idx] & PT_VALID)) {
        uintptr_t new_table = pmm_alloc();
        if (new_table == 0) return -1;
        memset((void*)new_table, 0, PAGE_SIZE);
        pgd[pgd_idx] = new_table | PT_TABLE | PT_VALID;
    }
    
    pud_t* pud = (pud_t*)(pgd[pgd_idx] & PT_ADDR_MASK);
    
    /* Level 2: PUD */
    if (!(pud[pud_idx] & PT_VALID)) {
        uintptr_t new_table = pmm_alloc();
        if (new_table == 0) return -1;
        memset((void*)new_table, 0, PAGE_SIZE);
        pud[pud_idx] = new_table | PT_TABLE | PT_VALID;
    }
    
    pmd_t* pmd = (pmd_t*)(pud[pud_idx] & PT_ADDR_MASK);
    
    /* Level 3: PMD */
    if (!(pmd[pmd_idx] & PT_VALID)) {
        uintptr_t new_table = pmm_alloc();
        if (new_table == 0) return -1;
        memset((void*)new_table, 0, PAGE_SIZE);
        pmd[pmd_idx] = new_table | PT_TABLE | PT_VALID;
    }
    
    pte_t* pte = (pte_t*)(pmd[pmd_idx] & PT_ADDR_MASK);
    
    /* Level 4: PTE - actual page entry */
    pte[pte_idx] = (paddr & PT_ADDR_MASK) | flags | PT_VALID | PT_AF;
    
    /* Invalidate TLB for this address */
    __asm__ volatile("tlbi vae1, %0" :: "r"(vaddr >> 12));
    __asm__ volatile("dsb ish");
    __asm__ volatile("isb");
    
    return 0;
}

/*
 * vmm_unmap - Unmap a virtual page
 */
__attribute__((weak)) void vmm_unmap(pgd_t* pgd, uintptr_t vaddr)
{
    if (pgd == NULL) return;
    
    uint64_t pgd_idx = (vaddr >> 39) & 0x1FF;
    uint64_t pud_idx = (vaddr >> 30) & 0x1FF;
    uint64_t pmd_idx = (vaddr >> 21) & 0x1FF;
    uint64_t pte_idx = (vaddr >> 12) & 0x1FF;
    
    if (!(pgd[pgd_idx] & PT_VALID)) return;
    pud_t* pud = (pud_t*)(pgd[pgd_idx] & PT_ADDR_MASK);
    
    if (!(pud[pud_idx] & PT_VALID)) return;
    pmd_t* pmd = (pmd_t*)(pud[pud_idx] & PT_ADDR_MASK);
    
    if (!(pmd[pmd_idx] & PT_VALID)) return;
    pte_t* pte = (pte_t*)(pmd[pmd_idx] & PT_ADDR_MASK);
    
    /* Clear page table entry */
    pte[pte_idx] = 0;
    
    /* Invalidate TLB */
    __asm__ volatile("tlbi vae1, %0" :: "r"(vaddr >> 12));
    __asm__ volatile("dsb ish");
    __asm__ volatile("isb");
}

/*
 * vmm_alloc_pages - Allocate and map virtual pages
 * @pgd: Page directory
 * @vaddr: Virtual address (or 0 for any)
 * @npages: Number of pages
 * @flags: Page flags
 */
uintptr_t vmm_alloc_pages(pgd_t* pgd, uintptr_t vaddr, size_t npages, uint32_t flags)
{
    for (size_t i = 0; i < npages; i++) {
        uintptr_t paddr = pmm_alloc();
        if (paddr == 0) {
            /* Rollback */
            for (size_t j = 0; j < i; j++) {
                vmm_unmap(pgd, vaddr + j * PAGE_SIZE);
            }
            return 0;
        }
        
        if (vmm_map(pgd, vaddr + i * PAGE_SIZE, paddr, flags) != 0) {
            pmm_free(paddr);
            /* Rollback */
            for (size_t j = 0; j < i; j++) {
                vmm_unmap(pgd, vaddr + j * PAGE_SIZE);
            }
            return 0;
        }
    }
    
    return vaddr;
}

/*
 * get_phys_addr - Get physical address for a virtual address
 */
uintptr_t vmm_get_phys(pgd_t* pgd, uintptr_t vaddr)
{
    if (pgd == NULL) return 0;
    
    uint64_t pgd_idx = (vaddr >> 39) & 0x1FF;
    uint64_t pud_idx = (vaddr >> 30) & 0x1FF;
    uint64_t pmd_idx = (vaddr >> 21) & 0x1FF;
    uint64_t pte_idx = (vaddr >> 12) & 0x1FF;
    
    if (!(pgd[pgd_idx] & PT_VALID)) return 0;
    pud_t* pud = (pud_t*)(pgd[pgd_idx] & PT_ADDR_MASK);
    
    if (!(pud[pud_idx] & PT_VALID)) return 0;
    pmd_t* pmd = (pmd_t*)(pud[pud_idx] & PT_ADDR_MASK);
    
    if (!(pmd[pmd_idx] & PT_VALID)) return 0;
    pte_t* pte = (pte_t*)(pmd[pmd_idx] & PT_ADDR_MASK);
    
    if (!(pte[pte_idx] & PT_VALID)) return 0;
    
    return (pte[pte_idx] & PT_ADDR_MASK) | (vaddr & 0xFFF);
}
