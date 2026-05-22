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
    /* Enable FP/SIMD */
    mov x0, #0x33FF
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
    
    /* Level 1 - Higher half mapping */
    ldr x1, =(__page_tables_start + 0x1000)
    ldr x2, =(__page_tables_start + 0x3000)  /* Level 2 */
    orr x2, x2, #0x3
    str x2, [x1, #0]
    
    /* Level 2 - 2MB block mappings for kernel */
    ldr x1, =(__page_tables_start + 0x3000)
    mov x2, #0x0               /* Start at physical 0 */
    
    /* Create 512 entries mapping 1GB */
    mov x3, #512
    mov x4, #(0x401 | (1 << 10) | (3 << 8))  /* AF=1, SH=3, AP=1 (RW) */
    
_pt_l2_loop:
    orr x5, x2, x4             /* Combine address with attributes */
    str x5, [x1], #8
    add x2, x2, #0x200000      /* Next 2MB block */
    sub x3, x3, #1
    cbnz x3, _pt_l2_loop
    
    /* Set TTBR0_EL1 (user space - identity) */
    ldr x0, =__page_tables_start
    msr TTBR0_EL1, x0
    
    /* Set TTBR1_EL1 (kernel space) */
    msr TTBR1_EL1, x0
    
    /* Configure TCR_EL1 */
    ldr x0, =0x80803520        /* 4K granule, 48-bit VA, 48-bit PA */
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
    
    /* Call C handler */
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
