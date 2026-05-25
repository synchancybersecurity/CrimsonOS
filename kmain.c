/* Copyright (c) 2026 SynChanCyberSecurity LLC. All Rights Reserved. */
/*
 * Crimson OS - Kernel Main Entry Point
 * 
 * This is where the C world begins. The assembly bootloader hands off
 * control here after setting up the CPU, MMU, and exception vectors.
 * 
 * Responsibilities:
 * - Initialize all kernel subsystems
 * - Set up drivers
 * - Start the scheduler
 * - Launch the Crimson Shell
 * - Hand off to user space when ready
 */

#include <crimson/types.h>
#include <crimson/memory.h>
#include <crimson/printk.h>
#include <crimson/scheduler.h>
#include <crimson/process.h>
#include <crimson/spinlock.h>
#include <crimson/driver.h>
#include <crimson/timer.h>
#include <crimson/interrupt.h>
#include <crimson/version.h>
#include <crimson/security.h>
#include <crimson/syscall.h>
#include <crimson/gui.h>
#include <crimson/phone.h>
#include <crimson/pkg.h>
#include <crimson/net.h>
#include <crimson/bloodmoon.h>
#include <crimson/crfs.h>
#include <crimson/fs.h>
#include <crimson/board.h>
#include <crimson/stubs.h>

/* External symbols from linker script */
extern uintptr_t __bss_start;
extern uintptr_t __bss_end;
extern uintptr_t __stack_top;
extern uintptr_t __heap_start;
extern uintptr_t __heap_end;
extern uintptr_t __kernel_size;

/* Boot banner - this is Crimson OS making its entrance */
static const char* crimson_banner[] = {
    "",
    "    ██████╗██████╗ ██╗███╗   ███╗███████╗ ██████╗ ███╗   ██╗",
    "   ██╔════╝██╔══██╗██║████╗ ████║██╔════╝██╔═══██╗████╗  ██║",
    "   ██║     ██████╔╝██║██╔████╔██║███████╗██║   ██║██╔██╗ ██║",
    "   ██║     ██╔══██╗██║██║╚██╔╝██║╚════██║██║   ██║██║╚██╗██║",
    "   ╚██████╗██║  ██║██║██║ ╚═╝ ██║███████║╚██████╔╝██║ ╚████║",
    "    ╚═════╝╚═╝  ╚═╝╚═╝╚═╝     ╚═╝╚══════╝ ╚═════╝ ╚═╝  ╚═══╝",
    "",
    "    ╔═══════════════════════════════════════════════════════╗",
    "    ║  Independent Mobile Operating System                 ║",
    "    ║  Secure. Open. Yours.                                ║",
    "    ╚═══════════════════════════════════════════════════════╝",
    "",
    NULL
};

/* Forward declarations */
static void crimson_init_memory(void);
static void crimson_init_drivers(void);
static void crimson_init_scheduler(void);
static void crimson_init_encryption(void);
static void crimson_init_security(void);
static void crimson_init_network(void);
static void crimson_init_filesystem(void);
static void crimson_init_gui(void);
static void crimson_init_phone(void);
static void crimson_init_packages(void);
static void crimson_show_banner(void);
void crimson_shell_task(void);
static void crimson_gui_task(void);
void crimson_idle_task(void);

/*
 * kernel_main - The C entry point
 * 
 * Called from boot.S with:
 *   x0 = heap_start (virtual address)
 *   x1 = heap_size (bytes)
 */
void kernel_main(uintptr_t heap_start, size_t heap_size)
{
    /* Disable interrupts during init */
    arch_disable_interrupts();

    /* Detect board platform (sets board_name, RAM, CPU count) */
    board_init_early();

    /* Initialize serial output ASAP for debug messages */
    uart_early_init();

    /* Wire printk to UART so all kernel messages appear on console */
    printk_init(uart_puts);

    /* Show the Crimson OS boot banner */
    crimson_show_banner();

    printk(KERN_INFO "Crimson OS v%s [%s]\n",
           CRIMSON_VERSION, CRIMSON_CODENAME);
    printk(KERN_INFO "Kernel: loaded at %p, size: %lu KB\n",
           (void*)0x40080000, (uint64_t)&__kernel_size / 1024);
    printk(KERN_INFO "Heap:   %p – %p  (%lu MB)\n",
           (void*)heap_start, (void*)(heap_start + heap_size),
           heap_size / (1024 * 1024));
    printk(KERN_INFO "CPU:    ARM64 (AArch64), %lu cores\n",
           cpu_get_core_count());
    printk(KERN_INFO "Board:  %s\n", board_get_name());
    
    /* ─── Phase 1: Core Subsystems ─── */
    printk(KERN_INFO "[INIT] Phase 1: Core subsystems...\n");
    
    printk(KERN_INFO "[INIT]   -> Memory manager...\n");
    crimson_init_memory();
    
    printk(KERN_INFO "[INIT]   -> Interrupt controller...\n");
    interrupt_init();
    
    printk(KERN_INFO "[INIT]   -> Timer subsystem...\n");
    timer_init();
    
    /* ─── Phase 2: Device Drivers ─── */
    printk(KERN_INFO "[INIT] Phase 2: Device drivers...\n");
    crimson_init_drivers();
    
    /* ─── Phase 3: Process & Scheduler ─── */
    printk(KERN_INFO "[INIT] Phase 3: Process management...\n");
    process_init();
    
    printk(KERN_INFO "[INIT]   -> Scheduler...\n");
    crimson_init_scheduler();
    
    /* ─── Phase 4: Security Layer ─── */
    printk(KERN_INFO "[INIT] Phase 4: Security subsystem...\n");
    crimson_init_security();
    crimson_init_encryption();
    
    /* ─── Phase 5: System Calls ─── */
    printk(KERN_INFO "[INIT] Phase 5: System calls...\n");
    syscall_init();
    
    /* ─── Phase 6: Network Stack ─── */
    printk(KERN_INFO "[INIT] Phase 6: Network stack...\n");
    crimson_init_network();
    
    /* ─── Phase 7: Filesystem ─── */
    printk(KERN_INFO "[INIT] Phase 7: Filesystem...\n");
    crimson_init_filesystem();
    
    /* ─── Phase 8: GUI / Display ─── */
    printk(KERN_INFO "[INIT] Phase 8: Display & GUI...\n");
    crimson_init_gui();
    
    /* ─── Phase 9: Phone Stack ─── */
    printk(KERN_INFO "[INIT] Phase 9: Phone stack...\n");
    crimson_init_phone();
    
    /* ─── Phase 10: Package Manager ─── */
    printk(KERN_INFO "[INIT] Phase 10: Package manager...\n");
    crimson_init_packages();
    
    /* ─── Phase 11: User Space ─── */
    printk(KERN_INFO "[INIT] Phase 11: Starting user space...\n");
    
    /* Create the Crimson Shell as PID 1 (init process) */
    printk(KERN_INFO "[INIT] Launching Crimson Shell (init)...\n");
    
    struct process_attr shell_attr = {
        .name = "crimson-shell",
        .entry = crimson_shell_task,
        .priority = PRIO_HIGH,
        .stack_size = 0x10000,      /* 64KB stack */
        .flags = PROC_FLAG_KERNEL,
    };
    
    pid_t shell_pid = process_create(&shell_attr);
    if (shell_pid < 0) {
        printk(KERN_CRIT "Failed to create shell process!\n");
        arch_hang();
    }
    printk(KERN_INFO "[INIT] Crimson Shell started as PID %d\n", shell_pid);
    
#ifndef BOARD_QEMU
    /* Create GUI render task (60fps compositor loop).
     * Skipped on QEMU — no framebuffer is initialised so gui_render_frame()
     * would write through a NULL fb pointer into the identity-mapped ROM at
     * address 0, corrupting it and eventually crashing at ELR=0x0. */
    struct process_attr gui_attr = {
        .name = "gui-render",
        .entry = crimson_gui_task,
        .priority = PRIO_HIGH,
        .stack_size = 0x8000,
        .flags = PROC_FLAG_KERNEL,
    };
    pid_t gui_pid = process_create(&gui_attr);
    printk(KERN_INFO "[INIT] GUI render task started as PID %d\n", gui_pid);
#else
    printk(KERN_INFO "[INIT] GUI render task: skipped (QEMU headless mode)\n");
#endif

    /* Create idle task for when nothing else runs */
    struct process_attr idle_attr = {
        .name = "idle",
        .entry = crimson_idle_task,
        .priority = PRIO_IDLE,
        .stack_size = 0x4000,
        .flags = PROC_FLAG_KERNEL,
    };
    process_create(&idle_attr);
    
    /* Enable interrupts - we're ready */
    arch_enable_interrupts();
    
    printk(KERN_INFO "=========================================\n");
    printk(KERN_INFO "  Crimson OS initialization complete\n");
    printk(KERN_INFO "  Handing control to scheduler...\n");
    printk(KERN_INFO "=========================================\n\n");
    
    /* 
     * Start the scheduler - this function never returns.
     * The scheduler will context switch to the shell task.
     */
    scheduler_start();
    
    /* Should never reach here */
    printk(KERN_CRIT "Scheduler returned unexpectedly!\n");
    arch_hang();
}

/*
 * Show the Crimson OS boot banner
 */
static void crimson_show_banner(void)
{
    for (int i = 0; crimson_banner[i] != NULL; i++) {
        uart_puts(crimson_banner[i]);
        uart_putc('\n');
    }
}

/*
 * Initialize the memory management subsystem
 */
static void crimson_init_memory(void)
{
    /* Initialize the physical page allocator */
    pmm_init((uintptr_t)&__heap_start, 
             (uintptr_t)&__heap_end - (uintptr_t)&__heap_start);
    
    /* Initialize the kernel heap allocator */
    kmalloc_init();
    
    printk(KERN_INFO "[INIT] Memory: %lu pages available (%lu MB)\n",
           pmm_get_free_pages(), 
           (pmm_get_free_pages() * PAGE_SIZE) / (1024*1024));
}

/*
 * Initialize all device drivers
 */
static void crimson_init_drivers(void)
{
    /* Platform-specific early driver registration */
    driver_register_all();
    
    /* Initialize GPIO subsystem */
    gpio_init();
    
    /* Initialize full UART driver */
    uart_init();
    
    /* Initialize timer for scheduling */
    timer_init_sched();
    
    /* Initialize random number generator (for crypto) */
    rng_init();
    
    printk(KERN_INFO "[INIT] All drivers initialized\n");
}

/*
 * Initialize the process scheduler
 */
static void crimson_init_scheduler(void)
{
    scheduler_init();
    
    /* Set up periodic timer tick (100Hz) */
    timer_set_callback(TIMER_ID_SCHED, (timer_callback_t)scheduler_tick);
    timer_start_periodic(TIMER_ID_SCHED, SCHED_HZ);
}

/*
 * Initialize the encryption subsystem
 */
static void crimson_init_encryption(void)
{
    /* Initialize the cryptographic engine */
    crypto_init();
    
    /* Set up secure key storage */
    keystore_init();
    
    printk(KERN_INFO "[INIT] Encryption: AES-256-GCM, ChaCha20-Poly1305, Ed25519 ready\n");
}

/*
 * The Crimson Shell task (PID 1 - init process)
 * This is the first user-facing process. It provides:
 * - Interactive command prompt
 * - System administration
 * - Process management interface
 * - Network configuration
 * - Security controls
 */
void crimson_shell_task(void)
{
    printk("\n");
    printk("╔═══════════════════════════════════════════════════════════╗\n");
    printk("║  Crimson Shell v0.1                                      ║\n");
    printk("║  Type 'help' for available commands                      ║\n");
    printk("╚═══════════════════════════════════════════════════════════╝\n");
    printk("\n");
    
    shell_run();
    
    /* Shell should never exit, but if it does... */
    printk(KERN_CRIT "Crimson Shell exited! System halted.\n");
    arch_hang();
}

/*
 * Idle task - runs when nothing else can
 * Puts CPU in low-power wait state
 */
void crimson_idle_task(void)
{
    while (1) {
        /* Wait For Event - puts CPU in low power until interrupt */
        arch_wfe();
    }
}

/*
 * Panic handler - called on fatal errors
 */
void kernel_panic(const char* msg)
{
    arch_disable_interrupts();
    
    printk(KERN_CRIT "\n");
    printk(KERN_CRIT "╔═══════════════════════════════════════════════════════════╗\n");
    printk(KERN_CRIT "║  KERNEL PANIC                                            ║\n");
    printk(KERN_CRIT "╠═══════════════════════════════════════════════════════════╣\n");
    printk(KERN_CRIT "║  %s\n", msg);
    printk(KERN_CRIT "╚═══════════════════════════════════════════════════════════╝\n");
    
    /* Print stack trace if available */
    debug_stack_trace();
    
    /* Halt the system */
    arch_hang();
}

/*
 * Initialize the security subsystem (MAC, sandbox, ASLR, CFI, NX)
 */
static void crimson_init_security(void)
{
    security_init();
}

/*
 * Initialize the network stack and BloodMoon browser
 */
static void crimson_init_network(void)
{
    net_init();
    bm_init();
    printk(KERN_INFO "[INIT]   -> BloodMoon browser ready\n");
}

/*
 * Initialize the CrimsonFS filesystem
 */
static void crimson_init_filesystem(void)
{
    crfs_init();
    fs_init();
}

/*
 * Initialize the GUI compositor and display
 */
static void crimson_init_gui(void)
{
#ifdef BOARD_QEMU
    /* No real display hardware on QEMU virt — skip DSI/framebuffer init.
     * The kernel continues to Phase 9+ and the shell runs headlessly. */
    printk(KERN_INFO "[INIT] Display: QEMU mode — skipping hardware init\n");
#else
    gui_init();
#endif
}

/*
 * Initialize the phone/cellular subsystem
 */
static void crimson_init_phone(void)
{
    phone_init(NULL);
}

/*
 * Initialize the package manager
 */
static void crimson_init_packages(void)
{
    pkg_init();
}

/*
 * GUI render task - 60fps compositor loop
 * Runs as a separate high-priority process
 */
static void crimson_gui_task(void)
{
    printk(KERN_INFO "[GUI-RENDER] Compositor task starting\n");

    /* Render a boot notification */
    gui_notify("System", "Crimson OS", "Welcome to Crimson OS v0.1.0", GUI_NOTIFY_INFO);

    uint32_t frame_count = 0;
    uint64_t last_fps_print = timer_get_uptime_seconds();

    while (1) {
        /* Render one frame */
        gui_render_frame();

        /* Process touch events */
        touch_event_t ev;
        while (touch_read_event_nb(&ev) > 0) {
            gui_handle_touch(&ev);
        }

        /* Swap display buffers (VSYNC) */
        display_swap_buffers();

        frame_count++;

        /* FPS counter (debug) */
        uint64_t now = timer_get_uptime_seconds();
        if (now - last_fps_print >= 5) {
            uint32_t fps = frame_count / 5;
            printk(KERN_DEBUG "[GUI-RENDER] FPS: %u | View: %u | Surfaces: %u\n",
                   fps, 0u, g_gui.surface_count);
            frame_count = 0;
            last_fps_print = now;
        }

        /* Yield to let other tasks run (cooperative scheduling) */
        /* ~60fps target = ~16ms per frame */
        scheduler_yield();
    }
}

/*
 * Architecture hang - infinite loop
 */
void arch_hang(void)
{
    while (1) {
        arch_wfe();
    }
}
