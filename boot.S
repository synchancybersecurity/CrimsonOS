/*
 * Crimson OS - Boot Entry (ARM64 Assembly)
 * 
 * This is the very first code executed when the CPU powers on.
 * It sets up the CPU environment, initializes memory, sets up
 * page tables, enables MMU, and jumps to the C kernel main.
 * 
 * Supports: Raspberry Pi 4, QEMU virt, and generic ARM64 hardware
 */

/* Constants for assembly — no C headers needed */
.equ PAGE_SIZE, 4096
.equ SCHED_HZ, 100

.section .boot, "ax"
.global _start
.global _hang
.global _el3_to_el1
.global _setup_page_tables
.global _enable_mmu

/* 
 * CPU IDs for supported hardware 
 */
.equ BCM2711_CPU_ID, 0x410FD083    /* Raspberry Pi 4 - Cortex-A72 */
.equ QEMU_VIRT_CPU_ID, 0x411FD073  /* QEMU - Cortex-A53/A57 */

/*
 * Entry point - _start
 * All cores jump here from the bootloader
 */
_start:
    /* Save device tree pointer (x0 = dtb phys address from bootloader) */
    mov x19, x0
    
    /* Read MPIDR_EL1 - Multiprocessor Affinity Register */
    mrs x0, MPIDR_EL1
    and x0, x0, #0xFF          /* Extract CPU ID ( Aff0 ) */
    
    /* Only CPU 0 continues; other cores wait */
    cbz x0, _cpu0_init
    
    /* Secondary cores: park and wait for signal */
_secondary_park:
    wfe                        /* Wait for event */
    ldr x1, =_secondary_start_addr
    ldr x2, [x1]
    cbz x2, _secondary_park    /* If no address, keep waiting */
    br x2                      /* Jump to assigned function */

/*
 * CPU 0 Initialization
 */
_cpu0_init:
    /* Set up stack pointer for EL2 */
    ldr x0, =__stack_top
    mov sp, x0
    
    /* Save device tree pointer */
    mov x0, x19
    
    /* Read current exception level */
    mrs x0, CurrentEL
    lsr x0, x0, #2
    and x0, x0, #3
    
    /* Check which EL we're at and configure accordingly */
    cmp x0, #3
    beq _from_el3
    cmp x0, #2
    beq _from_el2
    b _from_el1

/*
 * Entry from EL3 (Secure firmware)
 */
_from_el3:
    bl _el3_to_el1
    b _kernel_setup

/*
 * Entry from EL2 (Hypervisor)
 */
_from_el2:
    bl _el2_setup
    b _kernel_setup

/*
 * Entry from EL1 (Already in kernel mode)
 */
_from_el1:
    b _kernel_setup

/*
 * EL3 to EL1 transition
 * Configures the secure monitor and drops to EL1
 */
_el3_to_el1:
    /* Set up SCR_EL3 - Secure Configuration Register */
    mov x0, #0x5               /* NS=1, RW=1 (AArch64) */
    msr SCR_EL3, x0
    
    /* Set up SPSR_EL3 - Saved Program Status Register */
    mov x0, #0x3c9             /* EL1h, DAIF masked */
    msr SPSR_EL3, x0
    
    /* Set EL1 entry point */
    adr x0, _kernel_setup
    msr ELR_EL3, x0
    
    /* Configure EL2 before dropping */
    mov x0, #(1 << 31)         /* Enable AArch64 in EL1 */
    msr HCR_EL2, x0
    
    /* Set up EL1 system registers */
    msr SCTLR_EL1, xzr         /* Clear system control */
    
    /* Return from EL3 to EL1 */
    eret

/*
 * EL2 setup (if booted in EL2)
 */
_el2_setup:
    /* Enable AArch64 in EL1 */
    mov x0, #(1 << 31)
    msr HCR_EL2, x0
    
    /* Set SPSR for EL1 */
    mov x0, #0x3c9
    msr SPSR_EL2, x0
    
    /* Set EL1 entry */
    adr x0, _kernel_setup
    msr ELR_EL2, x0
    
    eret

/*
 * Main kernel setup - runs in EL1
 */
_kernel_setup:
    /* Set up stack for EL1 */
    ldr x0, =__stack_top
    mov sp, x0
    
    /* Save device tree pointer */
    mov x0, x19
    
    /* Zero BSS section */
    ldr x0, =__bss_start
    ldr x1, =__bss_end
    sub x2, x1, x0
    cbz x2, _bss_done
    
_bss_zero:
    str xzr, [x0], #8
    sub x2, x2, #8
    cbnz x2, _bss_zero
    
_bss_done:
    /* Set up exception vectors */
    ldr x0, =_exception_vectors
    msr VBAR_EL1, x0
    
    /* Initialize CPU features */
    bl _cpu_features_init
    
    /* Set up page tables and enable MMU */
    bl _setup_page_tables
    bl _enable_mmu
    
    /* Initialize interrupt controller */
    bl gic_init
    
    /* Enable interrupts */
    msr DAIFClr, #0xF
    
    /* Jump to C kernel main */
    ldr x0, =__bss_end
    ldr x1, =__heap_end
    sub x2, x1, x0             /* Calculate heap size */
    
    /* Call kernel_main(heap_start, heap_size) */
    ldr x0, =__bss_end         /* arg0: heap start */
    mov x1, x2                 /* arg1: heap size */
    bl kernel_main
    
    /* Should never return, but just in case */
    b _hang

/*
 * CPU Features Initialization
 */
_cpu_features_init:
    /* Enable FP/SIMD: CPACR_EL1.FPEN[21:20] = 11 (no trap at EL1/EL0) */
    mov x0, #(3 << 20)      /* FPEN = 11 */
    msr CPACR_EL1, x0
    isb
    
    /* Read CPU ID */
    mrs x0, MIDR_EL1
    
    /* Configure memory attributes */
    ldr x0, =0xFF440C0400      /* Normal, Device, Non-cacheable attrs */
    msr MAIR_EL1, x0
    
    ret

/*
 * Page Table Setup
 * Creates initial identity mapping and higher half mapping
 */
_setup_page_tables:
    /* Get page table base */
    ldr x0, =__page_tables_start
    
    /* Clear page tables */
    mov x1, x0
    mov x2, #0x400000          /* 4MB of page tables */
    lsr x2, x2, #3             /* /8 for 64-bit stores */
    
_pt_clear:
    str xzr, [x1], #8
    sub x2, x2, #1
    cbnz x2, _pt_clear
    
    /* Level 0 Page Table (PGD) - 512GB entries */
    /* Map kernel space: 0xFFFF000000000000 */
    ldr x1, =__page_tables_start
    ldr x2, =(__page_tables_start + 0x1000)  /* Level 1 table */
    
    /* Entry for higher half (index 510 for 0xFFFF000000000000) */
    orr x2, x2, #0x3           /* Valid + Table */
    str x2, [x1, #(510 * 8)]
    
    /* Identity map lower memory (index 0) */
    ldr x2, =(__page_tables_start + 0x2000)  /* Level 1 identity */
    orr x2, x2, #0x3
    str x2, [x1, #0]
    
    /* Page table layout (all within cleared 4MB area):
     *  PT_BASE+0x0000: PGD (L0, 512 entries)
     *  PT_BASE+0x1000: PUD_kernel  (L1 for TTBR1 high-half, PGD[510])
     *  PT_BASE+0x2000: PUD_identity (L1 for TTBR0, PGD[0])
     *  PT_BASE+0x3000: PMD_kernel  (L2 for PUD_kernel[0], maps 0-1GB)
     *  PT_BASE+0x4000: PMD_id_low  (L2 for PUD_identity[0], maps 0-1GB)
     *  PT_BASE+0x5000: PMD_id_high (L2 for PUD_identity[1], maps 1-2GB)
     * This gives a full 2GB identity map (0x0 to 0x7FFFFFFF), covering
     * the kernel at 0x40080000 and page tables at 0x40bXXXXX. */

    /* PUD_kernel[0] → PMD_kernel */
    ldr x1, =(__page_tables_start + 0x1000)
    ldr x2, =(__page_tables_start + 0x3000)
    orr x2, x2, #0x3
    str x2, [x1, #0]

    /* PUD_identity[0] → PMD_id_low */
    ldr x1, =(__page_tables_start + 0x2000)
    ldr x2, =(__page_tables_start + 0x4000)
    orr x2, x2, #0x3
    str x2, [x1, #0]

    /* PUD_identity[1] → PMD_id_high */
    ldr x2, =(__page_tables_start + 0x5000)
    orr x2, x2, #0x3
    str x2, [x1, #8]

    /* PMD_kernel + PMD_id_low: both map PA 0x0–0x3FFFFFFF with 2MB blocks */
    mov x4, #(0x401 | (1 << 10) | (3 << 8))    /* AF=1, SH=inner, valid+block */

    ldr x1, =(__page_tables_start + 0x3000)     /* PMD_kernel */
    mov x2, #0
    mov x3, #512
_pt_kern_loop:
    orr x5, x2, x4
    str x5, [x1], #8
    add x2, x2, #0x200000
    sub x3, x3, #1
    cbnz x3, _pt_kern_loop

    ldr x1, =(__page_tables_start + 0x4000)     /* PMD_id_low */
    mov x2, #0
    mov x3, #512
_pt_idlo_loop:
    orr x5, x2, x4
    str x5, [x1], #8
    add x2, x2, #0x200000
    sub x3, x3, #1
    cbnz x3, _pt_idlo_loop

    /* PMD_id_high: map PA 0x40000000–0x7FFFFFFF with 2MB blocks */
    ldr x1, =(__page_tables_start + 0x5000)     /* PMD_id_high */
    mov x2, #0x40000000
    mov x3, #512

_pt_idhi_loop:
    orr x5, x2, x4
    str x5, [x1], #8
    add x2, x2, #0x200000
    sub x3, x3, #1
    cbnz x3, _pt_idhi_loop

    /* Ensure all table writes are visible before programming TTBR */
    dsb ish

    /* Set TTBR0_EL1 (identity map: 0x0 – 0x7FFFFFFF) */
    ldr x0, =__page_tables_start
    msr TTBR0_EL1, x0

    /* Set TTBR1_EL1 (same PGD for high-half kernel space) */
    msr TTBR1_EL1, x0
    
    /* Configure TCR_EL1:
     *   T0SZ=16 (48-bit VA for TTBR0), T1SZ=16 (48-bit VA for TTBR1)
     *   TG0=0 (4KB), TG1=10 (4KB), SH0=10 (outer shareable)
     *   IRGN0=ORGN0=01 (write-back), EPD1=0 (TTBR1 enabled)
     *   IPS=0 (32-bit PA, sufficient for QEMU at 0x40000000)
     */
    ldr x0, =0x80103510
    msr TCR_EL1, x0
    isb
    
    ret

/*
 * Enable MMU
 */
_enable_mmu:
    /* Invalidate TLB */
    tlbi vmalle1
    dsb ish
    isb
    
    /* Enable MMU, caches, and alignment check */
    mrs x0, SCTLR_EL1
    orr x0, x0, #0x1          /* MMU enable */
    orr x0, x0, #(1 << 2)     /* Cache enable */
    orr x0, x0, #(1 << 12)    /* Instruction cache */
    msr SCTLR_EL1, x0
    isb
    
    ret

/*
 * Hang - infinite loop for errors or end of execution
 */
_hang:
    wfe
    b _hang

/*
 * Secondary core wake-up address storage
 */
.align 3
_secondary_start_addr:
    .quad 0x0

/*
 * Exception Vectors
 * Each entry is 128 bytes (0x80), total 2KB
 */
.align 11
_exception_vectors:
    /* Current EL with SP0 */
    b _hang                    /* Synchronous */
    .align 7
    b _hang                    /* IRQ */
    .align 7
    b _hang                    /* FIQ */
    .align 7
    b _hang                    /* SError */
    
    /* Current EL with SPx */
    .align 7
    b _sync_handler            /* Synchronous */
    .align 7
    b _irq_handler             /* IRQ */
    .align 7
    b _hang                    /* FIQ */
    .align 7
    b _hang                    /* SError */
    
    /* Lower EL in AArch64 */
    .align 7
    b _sync_handler
    .align 7
    b _irq_handler
    .align 7
    b _hang
    .align 7
    b _hang
    
    /* Lower EL in AArch32 */
    .align 7
    b _sync_handler
    .align 7
    b _irq_handler
    .align 7
    b _hang
    .align 7
    b _hang

/*
 * Synchronous Exception Handler
 */
_sync_handler:
    /* Save context */
    sub sp, sp, #256
    stp x0, x1, [sp, #0]
    stp x2, x3, [sp, #16]
    stp x4, x5, [sp, #32]
    stp x6, x7, [sp, #48]
    stp x8, x9, [sp, #64]
    stp x10, x11, [sp, #80]
    stp x12, x13, [sp, #96]
    stp x14, x15, [sp, #112]
    stp x16, x17, [sp, #128]
    stp x18, x19, [sp, #144]
    stp x20, x21, [sp, #160]
    stp x22, x23, [sp, #176]
    stp x24, x25, [sp, #192]
    stp x26, x27, [sp, #208]
    stp x28, x29, [sp, #224]
    str x30, [sp, #240]
    
    /* Pass ESR, ELR, SPSR to C handler */
    mrs x0, esr_el1
    mrs x1, elr_el1
    mrs x2, spsr_el1

    /* For external aborts (EC=0x24/0x25, DFSC[5:3]=0b010 = external abort):
     * advance ELR by 4 so we skip the faulting instruction and resume.
     * This lets hardware-absent writes/reads fail gracefully on QEMU. */
    ubfx x9, x0, #26, #6       /* x9 = EC = ESR[31:26] */
    cmp  x9, #0x24
    b.eq _check_external_abort
    cmp  x9, #0x25
    b.ne _sync_call_handler
_check_external_abort:
    ubfx x10, x0, #0, #6       /* x10 = DFSC = ISS[5:0] */
    ubfx x10, x10, #3, #3      /* x10 = DFSC[5:3] */
    cmp  x10, #0x2              /* 0b010 = external abort */
    b.ne _sync_call_handler
    add  x1, x1, #4             /* skip faulting instruction */
    msr  elr_el1, x1
_sync_call_handler:
    bl sync_exception_handler

    /* Restore context */
    ldp x0, x1, [sp, #0]
    ldp x2, x3, [sp, #16]
    ldp x4, x5, [sp, #32]
    ldp x6, x7, [sp, #48]
    ldp x8, x9, [sp, #64]
    ldp x10, x11, [sp, #80]
    ldp x12, x13, [sp, #96]
    ldp x14, x15, [sp, #112]
    ldp x16, x17, [sp, #128]
    ldp x18, x19, [sp, #144]
    ldp x20, x21, [sp, #160]
    ldp x22, x23, [sp, #176]
    ldp x24, x25, [sp, #192]
    ldp x26, x27, [sp, #208]
    ldp x28, x29, [sp, #224]
    ldr x30, [sp, #240]
    add sp, sp, #256
    
    eret

/*
 * IRQ Handler
 */
_irq_handler:
    /* Save context */
    sub sp, sp, #256
    stp x0, x1, [sp, #0]
    stp x2, x3, [sp, #16]
    stp x4, x5, [sp, #32]
    stp x6, x7, [sp, #48]
    stp x8, x9, [sp, #64]
    stp x10, x11, [sp, #80]
    stp x12, x13, [sp, #96]
    stp x14, x15, [sp, #112]
    stp x16, x17, [sp, #128]
    stp x18, x19, [sp, #144]
    stp x20, x21, [sp, #160]
    stp x22, x23, [sp, #176]
    stp x24, x25, [sp, #192]
    stp x26, x27, [sp, #208]
    stp x28, x29, [sp, #224]
    str x30, [sp, #240]
    
    /* Call C IRQ handler */
    bl irq_handler
    
    /* Restore context */
    ldp x0, x1, [sp, #0]
    ldp x2, x3, [sp, #16]
    ldp x4, x5, [sp, #32]
    ldp x6, x7, [sp, #48]
    ldp x8, x9, [sp, #64]
    ldp x10, x11, [sp, #80]
    ldp x12, x13, [sp, #96]
    ldp x14, x15, [sp, #112]
    ldp x16, x17, [sp, #128]
    ldp x18, x19, [sp, #144]
    ldp x20, x21, [sp, #160]
    ldp x22, x23, [sp, #176]
    ldp x24, x25, [sp, #192]
    ldp x26, x27, [sp, #208]
    ldp x28, x29, [sp, #224]
    ldr x30, [sp, #240]
    add sp, sp, #256
    
    eret

/*
 * syscall_entry - System call entry from userspace via svc #0
 * x8 = syscall number, x0-x5 = arguments
 * Returns result in x0
 */
.global syscall_entry
syscall_entry:
    /* Save userspace context */
    sub sp, sp, #256
    stp x0, x1, [sp, #0]
    stp x2, x3, [sp, #16]
    stp x4, x5, [sp, #32]
    stp x6, x7, [sp, #48]
    stp x8, x9, [sp, #64]
    stp x10, x11, [sp, #80]
    stp x12, x13, [sp, #96]
    stp x14, x15, [sp, #112]
    stp x16, x17, [sp, #128]
    stp x18, x19, [sp, #144]
    stp x20, x21, [sp, #160]
    stp x22, x23, [sp, #176]
    stp x24, x25, [sp, #192]
    stp x26, x27, [sp, #208]
    stp x28, x29, [sp, #224]
    str x30, [sp, #240]

    /* Move syscall number (x8) to x0 for C call, shift args */
    mov x6, x8           /* num -> x6 (temp) */
    ldr x7, [sp, #0]    /* load original x0 */
    mov x0, x6           /* num -> x0 (1st C arg) */
    mov x1, x7           /* orig x0 -> x1 (2nd C arg) */
    ldr x2, [sp, #8]    /* orig x1 -> x2 */
    ldr x3, [sp, #16]   /* orig x2 -> x3 */
    ldr x4, [sp, #24]   /* orig x3 -> x4 */
    ldr x5, [sp, #32]   /* orig x4 -> x5 */
    ldr x6, [sp, #40]   /* orig x5 -> x6 */

    bl syscall_dispatch

    /* Save return value */
    mov x9, x0

    /* Restore context */
    ldp x0, x1, [sp, #0]
    ldp x2, x3, [sp, #16]
    ldp x4, x5, [sp, #32]
    ldp x6, x7, [sp, #48]
    ldp x8, x9, [sp, #64]
    mov x0, x9           /* Return value in x0 */
    ldp x10, x11, [sp, #80]
    ldp x12, x13, [sp, #96]
    ldp x14, x15, [sp, #112]
    ldp x16, x17, [sp, #128]
    ldp x18, x19, [sp, #144]
    ldp x20, x21, [sp, #160]
    ldp x22, x23, [sp, #176]
    ldp x24, x25, [sp, #192]
    ldp x26, x27, [sp, #208]
    ldp x28, x29, [sp, #224]
    ldr x30, [sp, #240]
    add sp, sp, #256

    eret
