# Crimson OS

[![QEMU Boot](https://github.com/synchancybersecurity/CrimsonOS/actions/workflows/qemu-boot.yml/badge.svg)](https://github.com/synchancybersecurity/CrimsonOS/actions/workflows/qemu-boot.yml)
[![Release](https://img.shields.io/github/v/release/synchancybersecurity/CrimsonOS?label=release)](https://github.com/synchancybersecurity/CrimsonOS/releases)

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
- Interrupt controller init — GICv3 setup (RK3399)

**What is implemented and verified in code:**
- **Syscalls** — `sys_read`, `sys_write`, `sys_open`, `sys_close`, `sys_lseek` wired through VFS layer
- **CrimsonFS** — indirect + double-indirect block support; files up to ~1 GB
- **Crypto** — real AES-256-GCM using ARM64 `AESE`/`AESMC`/`PMULL` hardware instructions; CSPRNG is ChaCha20 seeded from `CNTPCT_EL0` generic timer
- **Virtual memory** — 4-level ARM64 page table walk: `vmm_unmap`, `vmm_copy_page_tables` (fork), `vmm_free_page_tables` (exit) with `TLBI` maintenance
- **Display pipeline (RK3399)** — DSI host init, D-PHY 4-lane 500 Mbps, XBD599/ST7703 panel init sequence for PinePhone Pro
- **Touch driver** — Goodix GT917S on RK3399 I2C5 (DesignWare); full contact decode (up to 10 fingers)
- **DNS resolution** — real RFC 1035 A-record query/response over UDP stack
- **HTTP GET** — full DNS → TCP connect → HTTP/1.0 request/response path
- **Network stack** — TCP/IP structure; not tested end-to-end on physical hardware

**What is written but not fully verified on physical hardware:**
- WiFi/Cellular — firmware loading code written, not tested on real hardware
- Display/Touch — implemented for PinePhone Pro RK3399, untested on device
- Full TCP/IP — stack logic written; no end-to-end hardware test yet

**What is explicitly stubbed (does not work yet):**
- Camera, USB — have TODO stubs, not functional
- Package execution — ELF loader written, not tested with real packages
- ChaCha20-Poly1305, Ed25519, SHA-256/512 — still stub implementations

---

## Hardware Target Correction

**Community feedback corrected a critical hardware error:** The PinePhone Pro uses the **Rockchip RK3399
