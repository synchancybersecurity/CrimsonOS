# Crimson OS

[![QEMU Boot](https://github.com/synchancybersecurity/CrimsonOS/actions/workflows/qemu-boot.yml/badge.svg)](https://github.com/synchancybersecurity/CrimsonOS/actions/workflows/qemu-boot.yml)

![Crimson OS Logo](IMG_0035.jpeg)

**Version:** 0.1.0-alpha "BloodMoon"
**Architecture:** ARM64 (AArch64)
**License:** GPLv3

> An independent mobile OS project. Original kernel codebase. Early alpha — honest about what works and what doesn't.

---

## What This Actually Is

Crimson OS is an original ARM64 kernel and mobile OS project — not a Linux fork, not Android-based. Written from scratch in C and ARM64 assembly.

**What actually boots and runs in QEMU:**
- ARM64 bootloader — EL3→EL1 transition, basic MMU setup
- Physical memory allocator — bitmap-based page allocator
- Basic process scheduler — MLFQ structure, timer-driven
- UART shell — 28 commands running on real hardware emulation
- Interrupt controller init — GICv2 setup

**What is written but not fully verified on hardware:**
- TCP/IP stack — RFC 793 structure, not tested end-to-end
- Filesystem (CrimsonFS) — has TODO stubs for indirect blocks
- Virtual memory manager — stub implementation, page walks incomplete
- Display pipeline — written, not wired to real display hardware
- Touch pipeline — written, not tested on real touchscreen hardware

**What is explicitly stubbed (does not work yet):**
- Crypto — `crypto_stub.c` copies plaintext, RNG is NOT cryptographically secure
- Most syscalls — `sys_read()`, `sys_write()`, `sys_open()` return `STATUS_UNIMPL`
- WiFi/Cellular — firmware loading code written, not tested on real hardware
- Camera, USB — have TODO stubs, not functional
- Package execution — ELF loader written, not tested with real packages

---

## Proof of Boot

The kernel compiles and boots in QEMU. Here is the verified output:

```
Crimson OS v0.1.0-alpha [BloodMoon]
CPU: ARM64 (AArch64), 4 cores detected
[INIT] Phase 1:  Core subsystems........✓
[INIT] Phase 2:  Device drivers.........✓
[INIT] Phase 3:  Process management.....✓
[INIT] Phase 4:  Security subsystem.....✓
[INIT] Phase 5:  System calls...........✓
[INIT] Phase 6:  Network stack..........✓
[INIT] Phase 7:  Filesystem.............✓
[INIT] Phase 8:  Display & GUI..........✓
[INIT] Phase 9:  Phone stack............✓
[INIT] Phase 10: Package manager........✓
[INIT] Phase 11: Starting user space....✓

crimson:~# help
[lists 28 shell commands]
crimson:~#
```

To verify yourself:

```bash
sudo apt install gcc-aarch64-linux-gnu qemu-system-aarch64
git clone https://github.com/synchancybersecurity/CrimsonOS
cd CrimsonOS
make
make qemu
```

---

## What Separates This From Linux

- Original bootloader, scheduler, and memory allocator — not Linux
- Capability-based MAC designed into the architecture from the start
- No inherited CVEs from Linux — also no 30 years of security hardening
- BloodMoon browser concept — Tor + I2P + Clear Net in one native browser

This is early-stage. The architecture decisions are real. The implementation is incomplete.

---

## About the GUI Demo

`index.html` is a **UI prototype** — an HTML/CSS/JS mockup showing what the OS interface will look like. It is not connected to the kernel. It demonstrates the design intent, not running code. It is clearly separate from the kernel source.

---

## About AI Assistance

This project used Claude (Anthropic) as a development partner throughout — for scaffolding, debugging build errors, implementing subsystems, and achieving first QEMU boot. The original kernel architecture and codebase concept was brought to the project by the founder of SynChan Cybersecurity.

AI assistance in this project means:
- Faster iteration on build fixes
- Help implementing subsystem scaffolding
- A tool for exploring kernel architecture patterns

It does not mean the kernel is production-ready or fully verified. The boot is real. The stubs are stubs. We are being transparent about both.

---

## Security Honesty

This kernel has not been audited. It has zero known CVEs because zero security researchers have looked at it yet — not because it has zero vulnerabilities. Do not use this for sensitive operations.

A professional security audit is required before any production use claim. That is what the Kickstarter funds.

---

## Build

```bash
sudo apt install gcc-aarch64-linux-gnu qemu-system-aarch64
git clone https://github.com/synchancybersecurity/CrimsonOS
cd CrimsonOS
make        # builds bin/crimson-os.img
make qemu   # boots in QEMU, Ctrl-A X to exit
```

---

## Contributing

Most needed:
- **PinePhone A64 display DSI driver** — blocking hardware boot
- **PinePhone touch I2C driver** — Goodix GT917S
- **Syscall implementations** — most return STATUS_UNIMPL
- **Crypto implementations** — replace stubs with real AES/Ed25519
- **Security audit** — find vulnerabilities, responsible disclosure to SynChanCyberSecurity@gmail.com

---

## Credits

Original kernel architecture and codebase by the founder of SynChan Cybersecurity.
Development partnership by SynChan AI (powered by Claude/Anthropic).

*Built by humans. Assisted by AI. Owned by the community.*

**Secure. Open. Yours.**

