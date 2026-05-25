/* Copyright (c) 2026 SynChanCyberSecurity LLC. All Rights Reserved. */
#ifndef _CRIMSON_SYSCALL_H
#define _CRIMSON_SYSCALL_H

#include <crimson/types.h>

/* Syscall numbers - match Linux ARM64 */
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
#define SYS_sched_yield             124
#define SYS_nanosleep               101
#define SYS_clone                   220
#define SYS_execve                  221
#define SYS_wait4                   260
#define SYS_ioctl                   29

/* Crimson extensions */
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

void syscall_init(void);
long syscall_dispatch(long num, long arg0, long arg1, long arg2,
                       long arg3, long arg4, long arg5);
void syscall_get_stats(void);

/* Userspace syscall wrappers (libc stubs) */
static inline long __syscall0(long n) {
    register long x8 __asm__("x8") = n;
    register long x0 __asm__("x0");
    __asm__ volatile("svc #0" : "=r"(x0) : "r"(x8));
    return x0;
}
static inline long __syscall1(long n, long a) {
    register long x8 __asm__("x8") = n;
    register long x0 __asm__("x0") = a;
    __asm__ volatile("svc #0" : "=r"(x0) : "r"(x8), "r"(x0));
    return x0;
}
static inline long __syscall2(long n, long a, long b) {
    register long x8 __asm__("x8") = n;
    register long x0 __asm__("x0") = a;
    register long x1 __asm__("x1") = b;
    __asm__ volatile("svc #0" : "=r"(x0) : "r"(x8), "r"(x0), "r"(x1));
    return x0;
}
static inline long __syscall3(long n, long a, long b, long c) {
    register long x8 __asm__("x8") = n;
    register long x0 __asm__("x0") = a;
    register long x1 __asm__("x1") = b;
    register long x2 __asm__("x2") = c;
    __asm__ volatile("svc #0" : "=r"(x0) : "r"(x8), "r"(x0), "r"(x1), "r"(x2));
    return x0;
}
static inline long __syscall6(long n, long a, long b, long c, long d, long e, long f) {
    register long x8 __asm__("x8") = n;
    register long x0 __asm__("x0") = a;
    register long x1 __asm__("x1") = b;
    register long x2 __asm__("x2") = c;
    register long x3 __asm__("x3") = d;
    register long x4 __asm__("x4") = e;
    register long x5 __asm__("x5") = f;
    __asm__ volatile("svc #0" : "=r"(x0) : "r"(x8), "r"(x0), "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5));
    return x0;
}

#endif
