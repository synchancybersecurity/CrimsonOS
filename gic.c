/* Copyright (c) 2026 SynChanCyberSecurity LLC. All Rights Reserved. */
/*
 * Crimson OS - Generic Interrupt Controller (GIC) Driver
 * 
 * ARM GICv2 driver for interrupt management.
 * The GIC is the central interrupt controller in ARM systems.
 * 
 * Components:
 * - Distributor: Routes interrupts to CPU interfaces
 * - CPU Interface: Handles interrupts for each core
 * 
 * Supports:
 * - SPI (Shared Peripheral Interrupts) - external devices
 * - PPI (Private Peripheral Interrupts) - per-core timers
 * - SGI (Software Generated Interrupts) - IPI between cores
 */

#include <crimson/types.h>
#include <crimson/interrupt.h>
#include <crimson/printk.h>
#include <crimson/asm.h>

/* GICv2 Base Addresses */
#ifdef BOARD_RPI4
  #define GICD_BASE       0xFF841000    /* Distributor */
  #define GICC_BASE       0xFF842000    /* CPU Interface */
#elif defined(BOARD_QEMU)
  #define GICD_BASE       0x08000000
  #define GICC_BASE       0x08010000
#else
  #define GICD_BASE       0x08000000
  #define GICC_BASE       0x08010000
#endif

/* GIC Distributor Registers */
#define GICD_CTLR       0x000   /* Distributor Control */
#define GICD_TYPER      0x004   /* Interrupt Controller Type */
#define GICD_IIDR       0x008   /* Distributor Implementer ID */
#define GICD_IGROUPR(n) (0x080 + (n) * 4)   /* Interrupt Group */
#define GICD_ISENABLER(n) (0x100 + (n) * 4) /* Set Enable */
#define GICD_ICENABLER(n) (0x180 + (n) * 4) /* Clear Enable */
#define GICD_ISPENDR(n) (0x200 + (n) * 4)   /* Set Pending */
#define GICD_ICPENDR(n) (0x280 + (n) * 4)   /* Clear Pending */
#define GICD_ISACTIVER(n) (0x300 + (n) * 4) /* Set Active */
#define GICD_ICACTIVER(n) (0x380 + (n) * 4) /* Clear Active */
#define GICD_IPRIORITYR(n) (0x400 + (n) * 4) /* Priority */
#define GICD_ITARGETSR(n) (0x800 + (n) * 4)  /* CPU Target */
#define GICD_ICFGR(n)   (0xC00 + (n) * 4)    /* Configuration */
#define GICD_NSACR(n)   (0xE00 + (n) * 4)    /* Non-secure Access */

/* GIC CPU Interface Registers */
#define GICC_CTLR       0x000   /* CPU Interface Control */
#define GICC_PMR        0x004   /* Priority Mask */
#define GICC_BPR        0x008   /* Binary Point */
#define GICC_IAR        0x00C   /* Interrupt Acknowledge */
#define GICC_EOIR       0x010   /* End of Interrupt */
#define GICC_RPR        0x014   /* Running Priority */
#define GICC_HPPIR      0x018   /* Highest Pending Priority */
#define GICC_ABPR       0x01C   /* Aliased Binary Point */
#define GICC_AIAR       0x020   /* Aliased IAR */
#define GICC_AEOIR      0x024   /* Aliased EOI */
#define GICC_AHPPIR     0x028   /* Aliased HPPIR */
#define GICC_APR0       0x0D0   /* Active Priorities */
#define GICC_NSAPR0     0x0E0   /* Non-secure Active Priorities */
#define GICC_IIDR       0x0FC   /* CPU Interface ID */
#define GICC_DIR        0x1000  /* Deactivate Interrupt */

/* Register access macros */
#define GICD_REG(off)   (*(volatile uint32_t*)((uintptr_t)gicd_base + (off)))
#define GICC_REG(off)   (*(volatile uint32_t*)((uintptr_t)gicc_base + (off)))

/* Number of IRQs (read from GICD_TYPER at runtime) */
static uint32_t gic_irq_lines = 0;

/* GIC base addresses */
static volatile uint32_t* gicd_base = NULL;
static volatile uint32_t* gicc_base = NULL;

/* IRQ handler table */
static irq_handler_t irq_handlers[512];
static void* irq_handler_data[512];

/* IRQ names for debugging */
static const char* irq_names[512];

/* Forward declarations */
static void gicd_enable_irq(uint32_t irq);
static void gicd_disable_irq(uint32_t irq);
static void gicd_set_priority(uint32_t irq, uint8_t prio);
static void gicd_set_target(uint32_t irq, uint8_t cpu_mask);
static void gicd_set_config(uint32_t irq, uint32_t config);

/*
 * gic_init - Initialize the GIC
 */
void gic_init(void)
{
    gicd_base = (volatile uint32_t*)GICD_BASE;
    gicc_base = (volatile uint32_t*)GICC_BASE;
    
    /* Read GIC type */
    uint32_t typer = GICD_REG(GICD_TYPER);
    gic_irq_lines = 32 * ((typer & 0x1F) + 1);
    
    /* Clear handler table */
    for (int i = 0; i < 512; i++) {
        irq_handlers[i] = NULL;
        irq_handler_data[i] = NULL;
        irq_names[i] = NULL;
    }
    
    /* Disable distributor */
    GICD_REG(GICD_CTLR) = 0;
    
    /* Disable CPU interface */
    GICC_REG(GICC_CTLR) = 0;
    
    /* Set priority mask (allow all priorities) */
    GICC_REG(GICC_PMR) = 0xFF;
    
    /* Set binary point (no preemption groups) */
    GICC_REG(GICC_BPR) = 0;
    
    /* Configure all SPIs as level-sensitive, 1-N model */
    for (uint32_t i = 2; i < (gic_irq_lines / 16); i++) {
        GICD_REG(GICD_ICFGR(i)) = 0;
    }
    
    /* Set default priorities */
    for (uint32_t i = 0; i < gic_irq_lines; i += 4) {
        GICD_REG(GICD_IPRIORITYR(i / 4)) = 0xA0A0A0A0;  /* Medium priority */
    }
    
    /* Route all SPIs to CPU 0 */
    for (uint32_t i = 32; i < gic_irq_lines; i += 4) {
        GICD_REG(GICD_ITARGETSR(i / 4)) = 0x01010101;  /* CPU 0 */
    }
    
    /* Enable distributor */
    GICD_REG(GICD_CTLR) = 1;
    
    /* Enable CPU interface */
    GICC_REG(GICC_CTLR) = 1;
    
    printk(KERN_DEBUG "GIC: %d IRQ lines, GICv2 initialized\n", gic_irq_lines);
}

/*
 * interrupt_register - Register an interrupt handler
 * @irq: IRQ number
 * @handler: Handler function
 * @name: Human-readable name (for debugging)
 */
void interrupt_register(uint32_t irq, irq_handler_t handler, const char* name)
{
    if (irq >= gic_irq_lines) return;
    
    irq_handlers[irq] = handler;
    irq_names[irq] = name;
}

/*
 * interrupt_register_with_data - Register handler with user data
 */
void interrupt_register_with_data(uint32_t irq, irq_handler_t handler, 
                                   void* data, const char* name)
{
    if (irq >= gic_irq_lines) return;
    
    irq_handlers[irq] = handler;
    irq_handler_data[irq] = data;
    irq_names[irq] = name;
}

/*
 * interrupt_enable - Enable an IRQ
 */
void interrupt_enable(uint32_t irq)
{
    if (irq >= gic_irq_lines) return;
    
    uint32_t reg = irq / 32;
    uint32_t bit = irq % 32;
    
    GICD_REG(GICD_ISENABLER(reg)) = (1 << bit);
}

/*
 * interrupt_disable - Disable an IRQ
 */
void interrupt_disable(uint32_t irq)
{
    if (irq >= gic_irq_lines) return;
    
    uint32_t reg = irq / 32;
    uint32_t bit = irq % 32;
    
    GICD_REG(GICD_ICENABLER(reg)) = (1 << bit);
}

/*
 * interrupt_set_priority - Set IRQ priority (0-255, lower = higher priority)
 */
void interrupt_set_priority(uint32_t irq, uint8_t priority)
{
    if (irq >= gic_irq_lines) return;
    
    uint32_t reg = irq / 4;
    uint32_t shift = (irq % 4) * 8;
    
    uint32_t val = GICD_REG(GICD_IPRIORITYR(reg));
    val &= ~(0xFF << shift);
    val |= ((uint32_t)priority << shift);
    GICD_REG(GICD_IPRIORITYR(reg)) = val;
}

/*
 * interrupt_set_target - Route IRQ to specific CPU(s)
 */
void interrupt_set_target(uint32_t irq, uint8_t cpu_mask)
{
    if (irq < 32 || irq >= gic_irq_lines) return;  /* SGIs and PPIs are per-CPU */
    
    uint32_t reg = irq / 4;
    uint32_t shift = (irq % 4) * 8;
    
    uint32_t val = GICD_REG(GICD_ITARGETSR(reg));
    val &= ~(0xFF << shift);
    val |= ((uint32_t)cpu_mask << shift);
    GICD_REG(GICD_ITARGETSR(reg)) = val;
}

/*
 * interrupt_trigger_sgi - Trigger Software Generated Interrupt
 * @sgi: SGI number (0-15)
 * @target_mask: Target CPU mask
 */
void interrupt_trigger_sgi(uint32_t sgi, uint8_t target_mask)
{
    if (sgi > 15) return;
    
    /* Write to GICD_SGIR */
    /* SGI is triggered by writing to GICD_SGIR (offset 0xF00) */
    GICD_REG(0xF00) = (target_mask << 16) | sgi;
}

/*
 * interrupt_init - Initialize interrupt subsystem (alias for gic_init)
 */
void interrupt_init(void)
{
    gic_init();
}

/* ─── Internal Functions ─── */

static void gicd_enable_irq(uint32_t irq)
{
    uint32_t reg = irq / 32;
    uint32_t bit = irq % 32;
    GICD_REG(GICD_ISENABLER(reg)) = (1 << bit);
}

static void gicd_disable_irq(uint32_t irq)
{
    uint32_t reg = irq / 32;
    uint32_t bit = irq % 32;
    GICD_REG(GICD_ICENABLER(reg)) = (1 << bit);
}

static void gicd_set_priority(uint32_t irq, uint8_t prio)
{
    uint32_t reg = irq / 4;
    uint32_t shift = (irq % 4) * 8;
    uint32_t val = GICD_REG(GICD_IPRIORITYR(reg));
    val &= ~(0xFF << shift);
    val |= ((uint32_t)prio << shift);
    GICD_REG(GICD_IPRIORITYR(reg)) = val;
}

static void gicd_set_target(uint32_t irq, uint8_t cpu_mask)
{
    uint32_t reg = irq / 4;
    uint32_t shift = (irq % 4) * 8;
    uint32_t val = GICD_REG(GICD_ITARGETSR(reg));
    val &= ~(0xFF << shift);
    val |= ((uint32_t)cpu_mask << shift);
    GICD_REG(GICD_ITARGETSR(reg)) = val;
}

static void gicd_set_config(uint32_t irq, uint32_t config)
{
    uint32_t reg = irq / 16;
    uint32_t shift = (irq % 16) * 2;
    uint32_t val = GICD_REG(GICD_ICFGR(reg));
    val &= ~(0x3 << shift);
    val |= (config << shift);
    GICD_REG(GICD_ICFGR(reg)) = val;
}

/* ─── Exception Handlers (called from assembly) ─── */

/*
 * irq_handler - Main IRQ handler (called from assembly vector table)
 */
void irq_handler(void)
{
    /* Read interrupt ID */
    uint32_t iar = GICC_REG(GICC_IAR);
    uint32_t irq = iar & 0x3FF;
    
    /* Spurious interrupt */
    if (irq >= 1020) {
        return;
    }
    
    /* Call registered handler */
    if (irq_handlers[irq] != NULL) {
        irq_handlers[irq](irq, irq_handler_data[irq]);
    } else {
        printk(KERN_WARN "Unhandled IRQ %d\n", irq);
    }
    
    /* End of interrupt */
    GICC_REG(GICC_EOIR) = iar;
}

/*
 * sync_exception_handler - Synchronous exception handler
 */
void sync_exception_handler(uint64_t esr, uint64_t elr, uint64_t spsr)
{
    uint32_t ec = (esr >> 26) & 0x3F;    /* Exception Class */
    uint32_t iss = esr & 0x1FFFFFF;      /* Instruction Specific Syndrome */
    
    printk(KERN_CRIT "Synchronous Exception!\n");
    printk(KERN_CRIT "  ESR: 0x%016lx\n", esr);
    printk(KERN_CRIT "  ELR: 0x%016lx\n", elr);
    printk(KERN_CRIT "  SPSR: 0x%016lx\n", spsr);
    printk(KERN_CRIT "  EC: 0x%02x, ISS: 0x%08x\n", ec, iss);
    
    /* Decode exception class */
    switch (ec) {
        case 0x00: printk(KERN_CRIT "  Unknown reason\n"); break;
        case 0x01: printk(KERN_CRIT "  Trapped WFI/WFE\n"); break;
        case 0x07: printk(KERN_CRIT "  SMC instruction\n"); break;
        case 0x0E: printk(KERN_CRIT "  Illegal Execution state\n"); break;
        case 0x15: 
            printk(KERN_CRIT "  SVC from AArch64 (syscall)\n");
            /* Handle system call */
            break;
        case 0x18: printk(KERN_CRIT "  MSR/MRS trapped\n"); break;
        case 0x20: 
        case 0x21:
            printk(KERN_CRIT "  Instruction abort\n"); 
            break;
        case 0x24:
        case 0x25:
            printk(KERN_CRIT "  Data abort\n"); 
            /* Check if it was a page fault we can handle */
            uint32_t dfsc = iss & 0x3F;
            printk(KERN_CRIT "  DFSC: 0x%02x\n", dfsc);
            break;
        case 0x2F: printk(KERN_CRIT "  Stack pointer alignment fault\n"); break;
        case 0x30: printk(KERN_CRIT "  Floating-point exception\n"); break;
        default:   printk(KERN_CRIT "  Unhandled exception class\n"); break;
    }
    
    /* If it's a system call, don't panic - handle it */
    if (ec == 0x15) {
        /* syscall_handler(elr, iss); */
        return;
    }
    
    /* External Aborts (EC 0x24-0x25) from non-existent hardware are
     * expected on QEMU where peripheral registers don't exist.
     * Treat them as recoverable so drivers can gracefully degrade. */
    if (ec == 0x24 || ec == 0x25) {
        uint32_t dfsc = iss & 0x3F;
        /* External abort (DFSC = 0b010000) — hardware not present */
        if ((dfsc & 0x38) == 0x10) {
            printk(KERN_WARN "  External abort at ELR=0x%lx (hardware absent, skipping)\n", elr);
            return;  /* return to instruction after the faulting one */
        }
    }

    /* Fatal exception */
    kernel_panic("Synchronous exception - cannot recover");
}

/*
 * arch_disable_interrupts - Disable IRQs
 */
void arch_disable_interrupts(void)
{
    __asm__ volatile("msr daifset, #0xF" ::: "memory");
}

/*
 * arch_enable_interrupts - Enable IRQs
 */
void arch_enable_interrupts(void)
{
    __asm__ volatile("msr daifclr, #0xF" ::: "memory");
}

/*
 * arch_save_irq_disable - Save IRQ state and disable
 */
uint64_t arch_save_irq_disable(void)
{
    uint64_t flags;
    __asm__ volatile(
        "mrs %0, daif\n"
        "msr daifset, #0xF\n"
        : "=r" (flags)
        :
        : "memory"
    );
    return flags;
}

/*
 * arch_restore_irq - Restore IRQ state
 */
void arch_restore_irq(uint64_t flags)
{
    __asm__ volatile("msr daif, %0" :: "r" (flags) : "memory");
}

/*
 * arch_wfe - Wait For Event (low power)
 */
void arch_wfe(void)
{
    __asm__ volatile("wfe");
}

/*
 * arch_trigger_reschedule - Trigger reschedule on another CPU (IPI)
 */
void arch_trigger_reschedule(uint32_t cpu)
{
    /* Send SGI 1 to target CPU (reschedule IPI) */
    interrupt_trigger_sgi(1, (1 << cpu));
}
