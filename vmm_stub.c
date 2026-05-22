/*
 * Crimson OS - VMM Free All (Stub)
 * 
 * Placeholder for vmm_free_all which is referenced in process.c
 * Will be replaced with full page table walk and free.
 */

#include <crimson/types.h>
#include <crimson/memory.h>

void vmm_free_all(pgd_t* pgd)
{
    /* TODO: Walk all page table levels and free pages */
    /* For now, just free the top-level page directory */
    if (pgd != NULL) {
        /* pmm_free((uintptr_t)pgd); */
    }
}
