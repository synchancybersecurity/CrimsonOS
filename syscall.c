/*
 * Crimson OS - System Call Interface
 *
 * The gateway between userspace and kernel. All userspace requests
 * flow through here. Provides a POSIX-compatible syscall table with
 * Crimson-specific extensions for security and mobile features.
 *
 * Syscall numbers are intentionally compatible with Linux ARM64
 * where possible, with Crimson extensions starting at 1024.
 */

#include <crimson/types.h>
#include <crimson/printk.h>
#include <crimson/process.h>
#include <crimson/scheduler.h>
#include <crimson/memory.h>
#include <crimson/spinlock.h>
#include <crimson/syscall.h>
#include <crimson/crfs.h>
#include <crimson/net.h>
#include <crimson/display.h>
#include <crimson/touch.h>
#include <crimson/audio.h>
#include <crimson/camera.h>
#include <crimson/power.h>
#include <crimson/crypto.h>
#include <crimson/timer.h>
#include <crimson/uart.h>
#include <crimson/display.h>

/* POSIX timespec - not available in freestanding */
struct timespec {
    long tv_sec;
    long tv_nsec;
};

/* ---- Syscall numbers (Linux ARM64 compatible + Crimson extensions) ---- */
#define SYS_read                    63
#define SYS_write                   64
#define SYS_open                    56
#define SYS_close                   57
#define SYS_lseek                   62
#define SYS_mmap                    222
#define SYS_munmap                  215
#define SYS_brk                     214
#define SYS_exit                    93
#define SYS_kill                    129
#define SYS_getpid                  172
#define SYS_getppid                 173
#define SYS_sched_yield             124
#define SYS_nanosleep               101
#define SYS_gettimeofday            169
#define SYS_socket                  198
#define SYS_bind                    200
#define SYS_connect                 203
#define SYS_listen                  201
#define SYS_accept                  202
#define SYS_sendto                  206
#define SYS_recvfrom                207
#define SYS_shutdown                210
#define SYS_clone                   220
#define SYS_execve                  221
#define SYS_wait4                   260
#define SYS_futex                   98
#define SYS_ioctl                   29

/* Crimson extensions (1024+) */
#define SYS_cr_display_info         1024
#define SYS_cr_fb_blit              1025
#define SYS_cr_touch_read           1026
#define SYS_cr_audio_open           1027
#define SYS_cr_audio_write          1028
#define SYS_cr_camera_capture       1029
#define SYS_cr_pm_get_battery       1030
#define SYS_cr_pm_set_governor      1031
#define SYS_cr_crypto_random        1032
#define SYS_cr_crypto_encrypt       1033
#define SYS_cr_net_connect          1034
#define SYS_cr_net_send             1035
#define SYS_cr_net_recv             1036
#define SYS_cr_proc_set_cap         1037
#define SYS_cr_display_brightness   1038
#define SYS_cr_cellular_signal      1039
#define SYS_cr_wifi_scan            1040

/* Max syscall */
#define NR_SYSCALLS                 1100

/* Syscall handler type */
typedef long (*syscall_fn_t)(long, long, long, long, long, long);

/* Forward declarations for handlers */
static long sys_read(long fd, long buf, long count, long, long, long);
static long sys_write(long fd, long buf, long count, long, long, long);
static long sys_open(long path, long flags, long mode, long, long, long);
static long sys_close(long fd, long, long, long, long, long);
static long sys_lseek(long fd, long offset, long whence, long, long, long);
static long sys_brk(long addr, long, long, long, long, long);
static long sys_mmap(long addr, long len, long prot, long flags, long fd, long off);
static long sys_munmap(long addr, long len, long, long, long, long);
static long sys_exit(long code, long, long, long, long, long);
static long sys_getpid(long, long, long, long, long, long);
static long sys_sched_yield(long, long, long, long, long, long);
static long sys_nanosleep(long req, long rem, long, long, long, long);
static long sys_kill(long pid, long sig, long, long, long, long);
static long sys_clone(long flags, long stack, long ptid, long ctid, long tls, long);
static long sys_wait4(long pid, long status, long options, long rusage, long, long);
static long sys_ioctl(long fd, long req, long arg, long, long, long);

/* Crimson extension handlers */
static long sys_cr_display_info(long w, long h, long pitch, long fb, long, long);
static long sys_cr_fb_blit(long dst_x, long dst_y, long w, long h, long src, long fmt);
static long sys_cr_touch_read(long ev, long, long, long, long, long);
static long sys_cr_audio_open(long type, long rate, long ch, long fmt, long, long);
static long sys_cr_pm_get_battery(long bat, long, long, long, long, long);
static long sys_cr_crypto_random(long buf, long len, long, long, long, long);
static long sys_cr_net_connect(long ip, long port, long, long, long, long);
static long sys_cr_proc_set_cap(long cap, long enable, long, long, long, long);

/* Syscall dispatch table */
static syscall_fn_t syscall_table[NR_SYSCALLS];

/* Statistics */
static uint64_t syscall_count[NR_SYSCALLS];
static spinlock_t syscall_lock = SPINLOCK_INIT;

/*
 * syscall_init - Build the syscall dispatch table
 */
void syscall_init(void)
{
    memset(syscall_table, 0, sizeof(syscall_table));
    memset(syscall_count, 0, sizeof(syscall_count));

    /* POSIX-compatible */
    syscall_table[SYS_read]         = sys_read;
    syscall_table[SYS_write]        = sys_write;
    syscall_table[SYS_open]         = sys_open;
    syscall_table[SYS_close]        = sys_close;
    syscall_table[SYS_lseek]        = sys_lseek;
    syscall_table[SYS_brk]          = sys_brk;
    syscall_table[SYS_mmap]         = sys_mmap;
    syscall_table[SYS_munmap]       = sys_munmap;
    syscall_table[SYS_exit]         = sys_exit;
    syscall_table[SYS_getpid]       = sys_getpid;
    syscall_table[SYS_sched_yield]  = sys_sched_yield;
    syscall_table[SYS_nanosleep]    = sys_nanosleep;
    syscall_table[SYS_kill]         = sys_kill;
    syscall_table[SYS_clone]        = sys_clone;
    syscall_table[SYS_wait4]        = sys_wait4;
    syscall_table[SYS_ioctl]        = sys_ioctl;

    /* Crimson extensions */
    syscall_table[SYS_cr_display_info]     = sys_cr_display_info;
    syscall_table[SYS_cr_fb_blit]          = sys_cr_fb_blit;
    syscall_table[SYS_cr_touch_read]       = sys_cr_touch_read;
    syscall_table[SYS_cr_audio_open]       = sys_cr_audio_open;
    syscall_table[SYS_cr_pm_get_battery]   = sys_cr_pm_get_battery;
    syscall_table[SYS_cr_crypto_random]    = sys_cr_crypto_random;
    syscall_table[SYS_cr_net_connect]      = sys_cr_net_connect;
    syscall_table[SYS_cr_proc_set_cap]     = sys_cr_proc_set_cap;

    printk(KERN_INFO "syscall: %d syscalls registered\n", NR_SYSCALLS);
}

/*
 * syscall_dispatch - Called from assembly when userspace does svc #0
 *
 * Arguments in x0-x5 (passed through)
 * Syscall number in x8
 * Return value in x0
 */
long syscall_dispatch(long num, long arg0, long arg1, long arg2,
                       long arg3, long arg4, long arg5)
{
    if (num < 0 || num >= NR_SYSCALLS || !syscall_table[num]) {
        printk(KERN_DEBUG "syscall: unimplemented syscall %ld\n", num);
        return -STATUS_UNIMPL;
    }

    spin_lock(&syscall_lock);
    syscall_count[num]++;
    spin_unlock(&syscall_lock);

    return syscall_table[num](arg0, arg1, arg2, arg3, arg4, arg5);
}

/*
 * syscall_get_stats - Print syscall usage statistics
 */
void syscall_get_stats(void)
{
    printk("\n=== Syscall Statistics ===\n");
    uint64_t total = 0;
    for (int i = 0; i < NR_SYSCALLS; i++) {
        if (syscall_count[i] > 0) {
            printk("  syscall %4d: %lu calls\n", i, syscall_count[i]);
            total += syscall_count[i];
        }
    }
    printk("  Total: %lu syscalls\n", total);
    printk("==========================\n\n");
}

/* ---- POSIX syscall handlers ---- */

static long sys_read(long fd, long buf, long count, long, long, long)
{
    (void)fd; (void)buf; (void)count;
    /* TODO: read from fd_table */
    return -STATUS_UNIMPL;
}

static long sys_write(long fd, long buf, long count, long, long, long)
{
    if (fd == 1 || fd == 2) {  /* stdout / stderr */
        const char* str = (const char*)(uintptr_t)buf;
        for (long i = 0; i < count; i++)
            uart_putc(str[i]);
        return count;
    }
    return -STATUS_UNIMPL;
}

static long sys_open(long path, long flags, long mode, long, long, long)
{
    return crfs_open((const char*)(uintptr_t)path, (uint32_t)flags, (uint32_t)mode);
}

static long sys_close(long fd, long, long, long, long, long)
{
    crfs_close((int)fd);
    return 0;
}

static long sys_lseek(long fd, long offset, long whence, long, long, long)
{
    (void)fd; (void)offset; (void)whence;
    return -STATUS_UNIMPL;
}

/* Userspace heap - brk-based allocation */
static uintptr_t user_heap_end = 0;

static long sys_brk(long addr, long, long, long, long, long)
{
    extern uintptr_t __heap_end;
    if (user_heap_end == 0)
        user_heap_end = (uintptr_t)&__heap_end;

    if (addr == 0)
        return (long)user_heap_end;

    if ((uintptr_t)addr > user_heap_end) {
        /* Allocate pages for expansion */
        size_t need = (uintptr_t)addr - user_heap_end;
        size_t pages = (need + PAGE_SIZE - 1) / PAGE_SIZE;
        for (size_t i = 0; i < pages; i++) {
            uintptr_t paddr = pmm_alloc();
            if (paddr == 0) return (long)user_heap_end;
            /* TODO: vmm_map user page */
            user_heap_end += PAGE_SIZE;
        }
    }
    return (long)user_heap_end;
}

static long sys_mmap(long addr, long len, long prot, long flags, long fd, long off)
{
    (void)addr; (void)prot; (void)flags; (void)fd; (void)off;
    /* Simple anonymous mmap */
    size_t pages = ((size_t)len + PAGE_SIZE - 1) / PAGE_SIZE;
    uintptr_t paddr = pmm_alloc_n(pages);
    if (paddr == 0) return -STATUS_NOMEM;
    memset((void*)paddr, 0, pages * PAGE_SIZE);
    return (long)paddr;
}

static long sys_munmap(long addr, long len, long, long, long, long)
{
    size_t pages = ((size_t)len + PAGE_SIZE - 1) / PAGE_SIZE;
    for (size_t i = 0; i < pages; i++)
        pmm_free((uintptr_t)addr + i * PAGE_SIZE);
    return 0;
}

static long sys_exit(long code, long, long, long, long, long)
{
    process_exit((int)code);
    return 0;  /* Never reached */
}

static long sys_getpid(long, long, long, long, long, long)
{
    struct process* p = scheduler_get_current();
    return p ? p->pid : 0;
}

static long sys_sched_yield(long, long, long, long, long, long)
{
    scheduler_yield();
    return 0;
}

static long sys_nanosleep(long req, long rem, long, long, long, long)
{
    (void)rem;
    struct timespec* ts = (struct timespec*)(uintptr_t)req;
    if (ts) {
        uint64_t ms = ts->tv_sec * 1000 + ts->tv_nsec / 1000000;
        timer_delay_ms((uint32_t)ms);
    }
    return 0;
}

static long sys_kill(long pid, long sig, long, long, long, long)
{
    return process_kill((pid_t)pid, (int)sig);
}

static long sys_clone(long flags, long stack, long ptid, long ctid, long tls, long)
{
    (void)flags; (void)stack; (void)ptid; (void)ctid; (void)tls;
    /* TODO: implement thread creation */
    return -STATUS_UNIMPL;
}

static long sys_wait4(long pid, long status, long options, long rusage, long, long)
{
    (void)rusage;
    int stat = 0;
    pid_t ret = process_wait((pid_t)pid, &stat, (int)options);
    if (status && ret > 0)
        *(int*)(uintptr_t)status = stat;
    return ret;
}

static long sys_ioctl(long fd, long req, long arg, long, long, long)
{
    (void)fd; (void)req; (void)arg;
    return -STATUS_UNIMPL;
}

/* ---- Crimson extension handlers ---- */

static long sys_cr_display_info(long w, long h, long pitch, long fb, long, long)
{
    uint32_t rw, rh, rp;
    void* rfb;
    display_get_info(&rw, &rh, &rp, &rfb);
    if (w) *(uint32_t*)(uintptr_t)w = rw;
    if (h) *(uint32_t*)(uintptr_t)h = rh;
    if (pitch) *(uint32_t*)(uintptr_t)pitch = rp;
    if (fb) *(void**)(uintptr_t)fb = rfb;
    return 0;
}

static long sys_cr_fb_blit(long dst_x, long dst_y, long w, long h, long src, long fmt)
{
    (void)fmt;
    display_blit((uint32_t)dst_x, (uint32_t)dst_y, (uint32_t)w, (uint32_t)h,
                 (const uint32_t*)(uintptr_t)src, (uint32_t)w * 4);
    return 0;
}

static long sys_cr_touch_read(long ev, long, long, long, long, long)
{
    touch_event_t* event = (touch_event_t*)(uintptr_t)ev;
    if (!event) return -STATUS_INVAL;
    return touch_read_event_nb(event);
}

static long sys_cr_audio_open(long type, long rate, long ch, long fmt, long, long)
{
    audio_stream_t* s = audio_open_stream((uint32_t)type, (uint32_t)rate,
                                           (uint32_t)ch, (uint32_t)fmt);
    return (long)(uintptr_t)s;
}

static long sys_cr_pm_get_battery(long bat, long, long, long, long, long)
{
    battery_state_t* b = (battery_state_t*)(uintptr_t)bat;
    if (b) pm_get_battery_status(b);
    return 0;
}

static long sys_cr_crypto_random(long buf, long len, long, long, long, long)
{
    rng_get_bytes((uint8_t*)(uintptr_t)buf, (size_t)len);
    return 0;
}

static long sys_cr_net_connect(long ip, long port, long, long, long, long)
{
    int fd = tcp_socket_create();
    if (fd < 0) return -STATUS_NOMEM;
    int ret = tcp_connect(fd, (uint32_t)ip, (uint16_t)port);
    if (ret < 0) {
        tcp_close(fd);
        return ret;
    }
    return fd;
}

static long sys_cr_proc_set_cap(long cap, long enable, long, long, long, long)
{
    struct process* p = scheduler_get_current();
    if (!p) return -STATUS_ERROR;
    if (cap < 0 || cap >= 64) return -STATUS_INVAL;
    uint64_t mask = 1ULL << cap;
    if (enable) {
        p->cap[0] |= mask;
    } else {
        p->cap[0] &= ~mask;
    }
    return 0;
}

/* timespec for nanosleep */
struct timespec {
    long tv_sec;
    long tv_nsec;
};
