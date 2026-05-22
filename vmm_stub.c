/*
 * Crimson OS - ARM64 Virtual Memory Manager
 *
 * Implements the page table walk operations required by the process lifecycle:
 *   vmm_free_all        - free all page tables (process exit)
 *   vmm_unmap           - unmap a single virtual page (declared in memory.h)
 *   vmm_unmap_page      - alias used internally by some callers
 *   vmm_copy_page_tables - deep copy for fork()
 *   vmm_free_page_tables - alias for vmm_free_all
 *
 * ARM64 4-level (4KB granule, 48-bit VA):
 *   VA[47:39] → PGD (L0, 512 entries)
 *   VA[38:30] → PUD (L1, 512 entries)
 *   VA[29:21] → PMD (L2, 512 entries)
 *   VA[20:12] → PTE (L3, 512 entries)
 *
 * In this kernel physical == virtual (identity mapped region), so
 * phys_to_ptr(pa) is simply (uint64_t*)pa.
 */

#include <crimson/types.h>
#include <crimson/memory.h>
#include <crimson/string.h>

/* ---- Constants ---- */
#define PTRS_PER_TABLE  512
#define PGD_SHIFT       39
#define PUD_SHIFT       30
#define PMD_SHIFT       21
#define PTE_SHIFT       12

#define PGD_IDX(va)  (((va) >> PGD_SHIFT) & 0x1FF)
#define PUD_IDX(va)  (((va) >> PUD_SHIFT) & 0x1FF)
#define PMD_IDX(va)  (((va) >> PMD_SHIFT) & 0x1FF)
#define PTE_IDX(va)  (((va) >> PTE_SHIFT) & 0x1FF)

/* Physical ↔ virtual in an identity-mapped kernel */
static inline uint64_t* phys_to_ptr(uintptr_t pa) { return (uint64_t*)pa; }
static inline uintptr_t  ptr_to_phys(const void* p) { return (uintptr_t)p; }

/* ---- TLB maintenance ---- */
static inline void tlb_flush_all(void)
{
    __asm__ volatile(
        "dsb    ish\n\t"
        "tlbi   vmalle1is\n\t"
        "dsb    ish\n\t"
        "isb"
        ::: "memory"
    );
}

static inline void tlb_flush_va(uintptr_t va)
{
    /* TLBI VAE1IS: by VA, EL1, inner shareable */
    __asm__ volatile(
        "dsb    ish\n\t"
        "tlbi   vae1is, %0\n\t"
        "dsb    ish\n\t"
        "isb"
        :: "r"(va >> 12)
        : "memory"
    );
}

/* ---- Allocate a zeroed page for a new table level ---- */
static uint64_t* alloc_table(void)
{
    uintptr_t pa = pmm_alloc();
    if (!pa) return NULL;
    memset((void*)pa, 0, PAGE_SIZE);
    return phys_to_ptr(pa);
}

/* ---- vmm_free_all ---- */
/*
 * Walk all four levels, free every mapped physical page, then free
 * all intermediate table pages, and finally the PGD itself.
 *
 * We only free leaf pages at the PTE level — block mappings at
 * PMD/PUD are not freed individually (the PMD/PUD pages themselves
 * are freed as table pages during the walk).
 */
void vmm_free_all(pgd_t* pgd)
{
    if (!pgd) return;
    uint64_t* pgd_tbl = (uint64_t*)pgd;

    for (int i = 0; i < PTRS_PER_TABLE; i++) {
        if (!(pgd_tbl[i] & PT_VALID)) continue;

        uint64_t* pud_tbl = phys_to_ptr(pgd_tbl[i] & PT_ADDR_MASK);

        for (int j = 0; j < PTRS_PER_TABLE; j++) {
            if (!(pud_tbl[j] & PT_VALID)) continue;

            uint64_t* pmd_tbl = phys_to_ptr(pud_tbl[j] & PT_ADDR_MASK);

            for (int k = 0; k < PTRS_PER_TABLE; k++) {
                if (!(pmd_tbl[k] & PT_VALID)) continue;

                uint64_t* pte_tbl = phys_to_ptr(pmd_tbl[k] & PT_ADDR_MASK);

                /* Free all physical pages at this PTE table */
                for (int l = 0; l < PTRS_PER_TABLE; l++) {
                    if (pte_tbl[l] & PT_VALID)
                        pmm_free(pte_tbl[l] & PT_ADDR_MASK);
                }
                pmm_free(ptr_to_phys(pte_tbl));   /* free PTE table page */
            }
            pmm_free(ptr_to_phys(pmd_tbl));        /* free PMD table page */
        }
        pmm_free(ptr_to_phys(pud_tbl));            /* free PUD table page */
    }
    pmm_free(ptr_to_phys(pgd_tbl));                /* free PGD page */

    tlb_flush_all();
}

/* ---- vmm_free_page_tables ---- */
/* Named variant used by process exit path */
void vmm_free_page_tables(pgd_t* pgd)
{
    vmm_free_all(pgd);
}

/* ---- vmm_unmap (declared in memory.h) ---- */
/*
 * Clear the PTE for `vaddr` and flush the TLB entry.
 * Does NOT free the underlying physical page — the caller owns that.
 */
void vmm_unmap(pgd_t* pgd, uintptr_t vaddr)
{
    if (!pgd) return;
    uint64_t* pgd_tbl = (uint64_t*)pgd;

    uint64_t pgd_e = pgd_tbl[PGD_IDX(vaddr)];
    if (!(pgd_e & PT_VALID)) return;

    uint64_t* pud_tbl = phys_to_ptr(pgd_e & PT_ADDR_MASK);
    uint64_t pud_e = pud_tbl[PUD_IDX(vaddr)];
    if (!(pud_e & PT_VALID)) return;

    uint64_t* pmd_tbl = phys_to_ptr(pud_e & PT_ADDR_MASK);
    uint64_t pmd_e = pmd_tbl[PMD_IDX(vaddr)];
    if (!(pmd_e & PT_VALID)) return;

    uint64_t* pte_tbl = phys_to_ptr(pmd_e & PT_ADDR_MASK);
    pte_tbl[PTE_IDX(vaddr)] = 0;

    /* Data synchronisation barrier before TLB invalidation */
    __asm__ volatile("dsb ishst" ::: "memory");
    tlb_flush_va(vaddr);
}

/* Named single-page variant referenced by some callers */
void vmm_unmap_page(pgd_t* pgd, uintptr_t vaddr)
{
    vmm_unmap(pgd, vaddr);
}

/* ---- vmm_copy_page_tables (fork) ---- */
/*
 * Deep-copy the entire address space of `src` into the freshly
 * allocated `dst` PGD.  For each mapped leaf page:
 *   1. Allocate a new physical page
 *   2. Copy the contents
 *   3. Map the new page in dst with the same flags
 *
 * Intermediate table pages (PUD, PMD, PTE) are also newly allocated.
 * The kernel region (VA ≥ 0xFFFF000000000000) is deliberately skipped
 * — kernel mappings are the same for all processes.
 */
int vmm_copy_page_tables(pgd_t* dst, const pgd_t* src)
{
    if (!dst || !src) return -1;

    uint64_t* dst_pgd = (uint64_t*)dst;
    const uint64_t* src_pgd = (const uint64_t*)src;

    for (int i = 0; i < PTRS_PER_TABLE; i++) {
        if (!(src_pgd[i] & PT_VALID)) continue;

        /* Skip kernel half of address space (upper canonical addresses) */
        uintptr_t va_base = (uintptr_t)i << PGD_SHIFT;
        if (va_base >= 0xFFFF000000000000UL) continue;

        const uint64_t* src_pud = phys_to_ptr(src_pgd[i] & PT_ADDR_MASK);
        uint64_t* dst_pud = alloc_table();
        if (!dst_pud) return -1;
        dst_pgd[i] = ptr_to_phys(dst_pud) | PT_VALID | PT_TABLE | PT_AF;

        for (int j = 0; j < PTRS_PER_TABLE; j++) {
            if (!(src_pud[j] & PT_VALID)) continue;

            const uint64_t* src_pmd = phys_to_ptr(src_pud[j] & PT_ADDR_MASK);
            uint64_t* dst_pmd = alloc_table();
            if (!dst_pmd) return -1;
            dst_pud[j] = ptr_to_phys(dst_pmd) | PT_VALID | PT_TABLE | PT_AF;

            for (int k = 0; k < PTRS_PER_TABLE; k++) {
                if (!(src_pmd[k] & PT_VALID)) continue;

                const uint64_t* src_pte = phys_to_ptr(src_pmd[k] & PT_ADDR_MASK);
                uint64_t* dst_pte = alloc_table();
                if (!dst_pte) return -1;
                dst_pmd[k] = ptr_to_phys(dst_pte) | PT_VALID | PT_TABLE | PT_AF;

                for (int l = 0; l < PTRS_PER_TABLE; l++) {
                    uint64_t pte = src_pte[l];
                    if (!(pte & PT_VALID)) continue;

                    uintptr_t src_pa = pte & PT_ADDR_MASK;

                    /* Allocate a new page and copy contents */
                    uintptr_t dst_pa = pmm_alloc();
                    if (!dst_pa) return -1;
                    memcpy(phys_to_ptr(dst_pa), phys_to_ptr(src_pa), PAGE_SIZE);

                    /* Preserve all flags, substitute new physical address */
                    dst_pte[l] = (pte & ~PT_ADDR_MASK) | dst_pa;
                }
            }
        }
    }

    /* Ensure table writes are visible before the new process runs */
    __asm__ volatile("dsb ish" ::: "memory");
    return 0;
}
