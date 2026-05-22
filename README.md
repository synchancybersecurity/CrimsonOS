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
- UART shell — 28 commands; `ls` and `cat` now read from CrimsonFS
- Interrupt controller init — GICv2 setup

**What is implemented and verified in code:**
- **Syscalls** — `sys_read`, `sys_write`, `sys_open`, `sys_close`, `sys_lseek` wired through VFS layer
- **CrimsonFS** — indirect + double-indirect block support; files up to ~1 GB
- **Crypto** — real AES-256-GCM using ARM64 `AESE`/`AESMC`/`PMULL` hardware instructions; CSPRNG is ChaCha20 seeded from `CNTPCT_EL0` generic timer
- **Virtual memory** — 4-level ARM64 page table walk: `vmm_unmap`, `vmm_copy_page_tables` (fork), `vmm_free_page_tables` (exit) with `TLBI` maintenance
- **Display pipeline (A64)** — DSI host init, D-PHY 4-lane 500 Mbps, XBD599/ST7703 panel init sequence for PinePhone Pro
- **Touch driver** — Goodix GT917S on A64 TWI1 I2C; full contact decode (up to 10 fingers)
- **DNS resolution** — real RFC 1035 A-record query/response over UDP stack
- **HTTP GET** — full DNS → TCP connect → HTTP/1.0 request/response path
- **Network stack** — TCP/IP structure; not tested end-to-end on physical hardware

**What is written but not fully verified on physical hardware:**
- WiFi/Cellular — firmware loading code written, not tested on real hardware
- Display/Touch — implemented for PinePhone Pro A64, untested on device
- Full TCP/IP — stack logic written; no end-to-end hardware test yet

**What is explicitly stubbed (does not work yet):**
- Camera, USB — have TODO stubs, not functional
- Package execution — ELF loader written, not tested with real packages
- ChaCha20-Poly1305, Ed25519, SHA-256/512 — still stub implementations

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

## Roadmap

| Milestone | Status |
|-----------|--------|
| ARM64 boot, MMU, scheduler, GICv2 | ✅ Complete |
| UART shell (28 commands) | ✅ Complete |
| CrimsonFS (direct blocks, log-structured) | ✅ Complete |
| CrimsonFS indirect/double-indirect blocks | ✅ Complete |
| VFS layer + syscalls (read/write/open/close/lseek) | ✅ Complete |
| ARM64 4-level page table (unmap, fork copy, free) | ✅ Complete |
| AES-256-GCM (ARM64 hw crypto) + ChaCha20 CSPRNG | ✅ Complete |
| A64 DSI display pipeline + XBD599 panel init | ✅ Complete |
| GT917S touch driver via A64 TWI1 I2C | ✅ Complete |
| DNS resolution (RFC 1035 UDP) + HTTP GET over TCP | ✅ Complete |
| Shell `ls`/`cat` reading from CrimsonFS | ✅ Complete |
| ChaCha20-Poly1305, Ed25519, SHA-256/512 | 🔲 Next |
| WiFi driver + `wpa_supplicant`-style association | 🔲 Next |
| Camera pipeline (ISP → CrimsonFS) | 🔲 Next |
| Package manager + ELF loader (userspace) | 🔲 Next |
| PinePhone Pro hardware boot (not QEMU) | 🔲 Next |
| Independent security audit | 🔲 Kickstarter goal |

---

## Contributing

Most needed right now:
- **Remaining crypto** — ChaCha20-Poly1305, Ed25519, SHA-256/512 stubs
- **WiFi** — nl80211-style association on A64 RTL8723CS
- **Camera** — ISP pipeline, V4L2-compatible capture interface
- **ELF loader / userspace** — run real binaries, not kernel threads
- **PinePhone hardware testing** — validate A64 DSI, GT917S, LTE modem
- **Security audit** — responsible disclosure: SynChanCyberSecurity@gmail.com

---

## Credits

**SynChan Cybersecurity** — original kernel architecture, design, and direction.  
Development accelerated with AI assistance (Claude / Anthropic).

Contact: SynChanCyberSecurity@gmail.com

*Built by humans. Assisted by AI. Owned by the community.*

**Secure. Open. Yours.**

