# Crimson OS Roadmap

**Version:** 0.1.0-alpha "BloodMoon"
**Architecture:** ARM64 (AArch64)
**Primary Hardware Target:** PinePhone Pro (Rockchip RK3399)

---

## Completed ✅

### Kernel Alpha — First Boot Verified (May 2026)
- [x] ARM64 bootloader — EL3→EL1 transition, MMU setup
- [x] Physical memory allocator — bitmap-based page allocator
- [x] MLFQ scheduler — 5 priority levels, 100 Hz timer-driven
- [x] GICv3 interrupt controller init (RK3399)
- [x] UART shell — 28 commands, `ls`/`cat` read from CrimsonFS
- [x] CrimsonFS — direct, indirect, and double-indirect blocks
- [x] VFS layer + syscalls (`read`/`write`/`open`/`close`/`lseek`)
- [x] ARM64 4-level page table walk with TLBI maintenance
- [x] AES-256-GCM using ARM64 hardware crypto instructions
- [x] ChaCha20 CSPRNG seeded from `CNTPCT_EL0`
- [x] RK3399 MIPI DSI display pipeline scaffold + XBD599 panel init
- [x] GT917S touch driver via RK3399 I2C5 (DesignWare)
- [x] DNS resolution — RFC 1035 A-record over UDP
- [x] HTTP GET — full DNS → TCP → HTTP/1.0 path
- [x] Security subsystem — MAC, ASLR, CFI, NX, stack canaries, sandbox
- [x] Zero-warning build on gcc `-Wall -Wextra`
- [x] QEMU virt boot — all 11 init phases verified

### Community & Infrastructure
- [x] Open sourced under GPLv3
- [x] GitHub CI/CD with QEMU boot verification badge
- [x] Community hardware correction (A64 → RK3399)

---

## In Progress 🔄

### Hardware Driver Rewrites (RK3399)
- [ ] `touch_gt917s.c` — A64 TWI1 → RK3399 DesignWare I2C5
- [ ] `wifi_rtl8723cs.c` — A64 MMC1 → RK3399 SDIO0 + CRU gates
- [ ] `modem_eg25g.c` — A64 UART3/GPIO → RK3399 UART2/GPIO4
- [ ] `power.c` — RPi4 PMIC → RK808 PMIC via I2C0
- [ ] `gic.c` — complete GICv3 redistributor init per-CPU

### Crypto Completion
- [ ] ChaCha20-Poly1305 AEAD
- [ ] Ed25519 signatures
- [ ] SHA-256 / SHA-512 hashing

---

## Next Phase 🔲

### Kickstarter — Fund the Audit
**Goal:** $25,000 | **Purpose:** Third-party security audit

### Month 1–3 — PinePhone Pro Hardware Boot
- WiFi (RTL8723CS on RK3399 SDIO0)
- LTE modem (EG25-G on RK3399 UART2)
- SD card boot image
- RK808 PMIC configuration

### Month 3–6 — Public Alpha 0.2.0
- BloodMoon browser on Tor/I2P/Clear
- Crimson Store with real packages
- NetWiz pentest suite on hardware
- Full phone stack (calls, SMS, data)
- Security audit published

### Month 6–12 — Beta + Multi-Device
- Sony Xperia, Librem 5 ports
- Community contributor program
- OTA updates

### Year 2 — Crimson Hardware v1
- Purpose-built security device ($100K+ stretch goal)
- FCC/CE certification

---

## How to Contribute

```bash
git clone https://github.com/synchancybersecurity/CrimsonOS
cd CrimsonOS
make BOARD=qemu
make qemu
