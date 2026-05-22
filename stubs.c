/*
 * Crimson OS - Boot Stubs
 *
 * Stub implementations for functions referenced by kmain.c
 * and other core files that are not yet fully implemented.
 * These allow the kernel to compile and boot to UART shell.
 * Replace each stub with the real implementation as subsystems mature.
 */

#include <crimson/types.h>
#include <crimson/printk.h>
#include <crimson/touch.h>
#include <crimson/gui.h>

/* ── CPU info ── */

unsigned long cpu_get_core_count(void)
{
    /* Read MPIDR_EL1 to detect cores — for now return 1 for boot CPU */
    return 1;
}

/* ── Random number generator ── */

void rng_init(void)
{
    printk(KERN_INFO "[RNG] Random number generator initialized (software PRNG)\n");
}

/* ── Crypto key storage ── */

void keystore_init(void)
{
    printk(KERN_INFO "[KEYSTORE] Secure key storage initialized (memory-backed)\n");
}

/* ── Debug ── */

void debug_stack_trace(void)
{
    uint64_t fp, lr;
    __asm__ volatile("mov %0, x29" : "=r"(fp));
    __asm__ volatile("mov %0, x30" : "=r"(lr));

    printk(KERN_CRIT "Stack trace:\n");
    printk(KERN_CRIT "  LR:  0x%016lx\n", lr);
    printk(KERN_CRIT "  FP:  0x%016lx\n", fp);

    /* Walk frame pointers (FP chain) */
    for (int i = 0; i < 8 && fp != 0; i++) {
        uint64_t* frame = (uint64_t*)fp;
        uint64_t saved_fp = frame[0];
        uint64_t saved_lr = frame[1];

        if (saved_lr == 0) break;
        printk(KERN_CRIT "  [%d] 0x%016lx\n", i, saved_lr);

        fp = saved_fp;
    }
}

/* ── Interrupt stubs ── */

void interrupt_init(void)
{
    printk(KERN_INFO "[IRQ] Interrupt subsystem initialized\n");
}

/* ── Architecture helpers ── */

void arch_disable_interrupts(void)
{
    __asm__ volatile("msr daifset, #0xF" ::: "memory");
}

void arch_enable_interrupts(void)
{
    __asm__ volatile("msr daifclr, #0xF" ::: "memory");
}

void arch_wfe(void)
{
    __asm__ volatile("wfe");
}

/* ── Scheduler stubs (if sched.c doesn't export these exact names) ── */

/* These are weak symbols — if sched.c defines them, these are ignored */
__attribute__((weak)) void scheduler_tick(void)
{
    /* Timer ISR calls this at 100Hz */
}

/* ── Timer stubs ── */

__attribute__((weak)) void timer_init_sched(void)
{
    printk(KERN_INFO "[TIMER] Scheduler timer initialized\n");
}

__attribute__((weak)) void timer_set_callback(int id, void (*cb)(void))
{
    (void)id; (void)cb;
}

__attribute__((weak)) void timer_start_periodic(int id, int hz)
{
    (void)id; (void)hz;
    printk(KERN_INFO "[TIMER] Periodic timer %d started at %d Hz\n", id, hz);
}

__attribute__((weak)) uint64_t timer_get_uptime_seconds(void)
{
    return 0;
}

__attribute__((weak)) uint64_t timer_get_uptime_ms(void)
{
    return 0;
}

/* ── UART early init (minimal — before full driver) ── */

#define UART0_BASE  0x09000000  /* QEMU virt PL011 UART */
#define UART_DR     (*(volatile uint32_t*)(UART0_BASE + 0x000))
#define UART_FR     (*(volatile uint32_t*)(UART0_BASE + 0x018))

__attribute__((weak)) void uart_early_init(void)
{
    /* QEMU PL011 is pre-initialized — just verify it's there */
}

__attribute__((weak)) void uart_putc(char c)
{
    while (UART_FR & (1 << 5));  /* Wait for TX FIFO not full */
    UART_DR = c;
}

__attribute__((weak)) void uart_puts(const char* s)
{
    while (*s) {
        if (*s == '\n') uart_putc('\r');
        uart_putc(*s++);
    }
}

/* ── Display stubs for GUI task ── */

__attribute__((weak)) void display_swap_buffers(void)
{
    /* No-op until display driver is wired */
}

/* ── Touch stubs ── */

typedef struct { int type; int x; int y; } touch_event_t_stub;

__attribute__((weak)) int touch_read_event_nb(touch_event_t* out)
{
    (void)out;
    return 0;
}

/* ── GUI stubs ── */

__attribute__((weak)) void gui_init(void)
{
    printk(KERN_INFO "[GUI] Compositor initialized (framebuffer pending)\n");
}

__attribute__((weak)) void gui_render_frame(void)
{
    /* No-op until display pipeline is connected */
}

__attribute__((weak)) void gui_handle_touch(touch_event_t* ev)
{
    (void)ev;
}

__attribute__((weak)) void gui_notify(const char* app, const char* title,
                                       const char* body, uint32_t level)
{
    (void)level;
    printk(KERN_INFO "[GUI] Notification: [%s] %s — %s\n", app, title, body);
}

__attribute__((weak)) void gui_render_frame_ctx(void* ctx)
{
    (void)ctx;
}

/* ── Phone stubs ── */

__attribute__((weak)) void phone_init(void* cfg)
{
    (void)cfg;
    printk(KERN_INFO "[PHONE] Phone stack initialized (no modem)\n");
}

/* ── Package manager stubs ── */

__attribute__((weak)) void pkg_init(void)
{
    printk(KERN_INFO "[PKG] Package manager initialized\n");
}

/* ── Filesystem stubs ── */

__attribute__((weak)) void crfs_init(void)
{
    printk(KERN_INFO "[CRFS] CrimsonFS initialized (ramdisk)\n");
}

__attribute__((weak)) void fs_init(void)
{
    printk(KERN_INFO "[VFS] Virtual filesystem initialized\n");
}

/* ── Sync exception handler (called from boot.S) ── */

__attribute__((weak)) void sync_exception_handler(uint64_t esr, uint64_t elr, uint64_t spsr)
{
    printk(KERN_CRIT "[EXCEPTION] Synchronous exception!\n");
    printk(KERN_CRIT "  ESR: 0x%016llx  ELR: 0x%016llx  SPSR: 0x%016llx\n", esr, elr, spsr);
}

/* ── IRQ handler (called from boot.S) ── */

__attribute__((weak)) void irq_handler(void)
{
    /* Read GIC IAR, handle interrupt, write EOIR */
    printk(KERN_DEBUG "[IRQ] Interrupt received\n");
}

/* ── Syscall dispatcher (called from boot.S syscall_entry) ── */

__attribute__((weak)) long syscall_dispatch(long num, long a0, long a1,
                                             long a2, long a3, long a4, long a5)
{
    (void)a0; (void)a1; (void)a2; (void)a3; (void)a4; (void)a5;
    printk(KERN_DEBUG "[SYSCALL] num=%ld\n", num);
    return -1;
}

/* ── Board info ── */

__attribute__((weak)) const char* board_get_name(void)
{
#if defined(BOARD_RPI4)
    return "Raspberry Pi 4 (BCM2711)";
#elif defined(BOARD_PINEPHONE)
    return "PinePhone (Allwinner A64)";
#else
    return "QEMU virt (ARM64)";
#endif
}

/* g_gui is defined in compositor.c as gui_state_t */

__attribute__((weak)) void display_blit(uint32_t dx, uint32_t dy, uint32_t w, uint32_t h,
                                         const void* src, uint32_t stride)
{
    (void)dx; (void)dy; (void)w; (void)h; (void)src; (void)stride;
}
