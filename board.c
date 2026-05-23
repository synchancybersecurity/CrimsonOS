/*
 * Crimson OS - Board Detection and Platform Setup
 * 
 * Detects the hardware platform at boot time and configures
 * the kernel accordingly. Supports multiple ARM64 platforms.
 */

#include <crimson/types.h>
#include <crimson/board.h>
#include <crimson/process.h>
#include <crimson/printk.h>
#include <crimson/string.h>

/* Detected board */
static const char* board_name = "Unknown";
static uint32_t board_ram = 0;
static uint32_t board_cpus = 4;

/*
 * board_get_name - Return detected board name
 */
const char* board_get_name(void)
{
    return board_name;
}

/*
 * board_get_ram_size - Return total RAM size
 */
uint32_t board_get_ram_size(void)
{
    return board_ram;
}

/*
 * board_get_cpu_cores - Return number of CPU cores
 */
uint32_t board_get_cpu_cores(void)
{
    return board_cpus;
}

/*
 * board_init_early - Very early board detection
 * Called before UART is initialized
 */
void board_init_early(void)
{
    /* Try to detect board from device tree or CPU registers */
    /* For now, use compile-time defaults */
    
#ifdef BOARD_RPI4
    board_name = "Raspberry Pi 4";
    board_ram = 4096;  /* 4GB default */
    board_cpus = 4;
#elif defined(BOARD_RPI3)
    board_name = "Raspberry Pi 3";
    board_ram = 1024;
    board_cpus = 4;
#elif defined(BOARD_QEMU)
    board_name = "QEMU ARM64 Virt";
    board_ram = 2048;
    board_cpus = 4;
#elif defined(BOARD_PINEPHONE)
    board_name = "PinePhone (Allwinner A64)";
    board_ram = 2048;
    board_cpus = 4;
#elif defined(BOARD_PINEPHONE_PRO)
    board_name = "PinePhone Pro (RK3399)";
    board_ram = 4096;
    board_cpus = 6;
#elif defined(BOARD_LIBREM5)
    board_name = "Librem 5 (i.MX8MQ)";
    board_ram = 3072;
    board_cpus = 4;
#else
    board_name = "Generic ARM64";
    board_ram = 2048;
    board_cpus = 4;
#endif
}

/*
 * board_init - Full board initialization
 */
void board_init(void)
{
    printk(KERN_INFO "Board: %s (%d cores, %d MB RAM)\n",
           board_name, board_cpus, board_ram);
    
    /* Platform-specific initialization would go here */
    /* e.g., PMIC setup, display controller init, etc. */
}

/*
 * dtb_parse - Parse device tree blob
 */
void dtb_parse(void* dtb_addr)
{
    if (dtb_addr == NULL) return;
    
    /* Device tree parsing for hardware detection */
    /* FDT (Flattened Device Tree) format parsing */
    
    /* TODO: Implement full device tree parser */
    /* This would read memory layout, CPU info, peripheral addresses */
}

/*
 * cpu_get_id - Get current CPU core ID
 */
uint32_t cpu_get_id(void)
{
    uint64_t mpidr;
    __asm__ volatile("mrs %0, mpidr_el1" : "=r" (mpidr));
    return (uint32_t)(mpidr & 0xFF);
}

/*
 * cpu_get_core_count - Get total number of CPU cores
 */
uint32_t cpu_get_core_count(void)
{
    return board_cpus;
}

/*
 * arch_context_switch - Switch CPU context between processes
 * Implemented in assembly for precise register control
 */
void arch_context_switch(struct process* prev, struct process* next)
{
    /* Save current context to prev */
    /* Restore next context */
    /* This would be implemented in boot.S or a separate asm file */
    
    /* Minimal implementation: */
    __asm__ volatile(
        /* Save callee-saved registers */
        "stp x19, x20, [%[prev_ctx], #0]\n"
        "stp x21, x22, [%[prev_ctx], #16]\n"
        "stp x23, x24, [%[prev_ctx], #32]\n"
        "stp x25, x26, [%[prev_ctx], #48]\n"
        "stp x27, x28, [%[prev_ctx], #64]\n"
        "stp x29, x30, [%[prev_ctx], #80]\n"
        "mov x0, sp\n"
        "str x0, [%[prev_ctx], #96]\n"   /* Save SP */
        
        /* Load next context */
        "ldp x19, x20, [%[next_ctx], #0]\n"
        "ldp x21, x22, [%[next_ctx], #16]\n"
        "ldp x23, x24, [%[next_ctx], #32]\n"
        "ldp x25, x26, [%[next_ctx], #48]\n"
        "ldp x27, x28, [%[next_ctx], #64]\n"
        "ldp x29, x30, [%[next_ctx], #80]\n"
        "ldr x0, [%[next_ctx], #96]\n"
        "mov sp, x0\n"                    /* Restore SP */
        
        "ret\n"
        :
        : [prev_ctx] "r" (&prev->ctx), [next_ctx] "r" (&next->ctx)
        : "x0", "memory"
    );
}

/*
 * arch_jump_to_process - Initial jump to first process
 */
void arch_jump_to_process(struct process* proc)
{
    /* Set up stack and jump to process entry point */
    __asm__ volatile(
        "ldr x0, [%[ctx], #96]\n"     /* SP */
        "mov sp, x0\n"
        "ldr x0, [%[ctx], #88]\n"     /* PC (x30) */
        "blr x0\n"
        :
        : [ctx] "r" (&proc->ctx)
        : "x0"
    );
}

/*
 * debug_stack_trace - Print stack trace on panic
 */
void debug_stack_trace(void)
{
    /* Read frame pointer and walk the stack */
    uint64_t fp, lr;
    
    __asm__ volatile("mov %0, x29" : "=r" (fp));
    
    printk("Stack trace:\n");
    for (int i = 0; i < 16 && fp != 0; i++) {
        /* LR is stored at FP + 8 */
        lr = *(uint64_t*)(fp + 8);
        printk("  #%d: 0x%016lx\n", i, lr);
        
        /* Previous FP is at FP */
        fp = *(uint64_t*)fp;
    }
}
