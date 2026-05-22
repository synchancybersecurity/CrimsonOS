# Crimson OS - Subsystem Reference

## Total Codebase: 13,684 lines of kernel C/Assembly

---

## 1. Boot Layer (`boot/boot.S`) — 446 lines
ARM64 assembly — the very first code executed on CPU reset.
- Exception level transitions (EL3 -> EL1)
- MMU page table setup (identity + higher-half mapping)
- Full exception vector table (sync, IRQ, FIQ, SError)
- Context switch in assembly (save/restore all 31 registers)
- Secondary CPU core parking and IPI wake-up

## 2. Memory Management (`kernel/mm.c`) — 534 lines
- **Physical Memory Manager**: Bitmap-based page frame allocator, O(1) alloc/free
- **Kernel Heap**: kmalloc/kfree with block splitting, coalescing, best-fit
- **Virtual Memory Manager**: 4-level page tables, vmm_map/unmap/page walks

## 3. Process Scheduler (`kernel/sched.c`) — 569 lines
- **Multi-Level Feedback Queue (MLFQ)**: 5 priority levels (RT/High/Normal/Low/Idle)
- O(1) task selection via 5-bit bitmap
- Preemptive at 100Hz, priority boosting every 2 seconds
- Per-CPU runqueues with load balancing
- Wait queues for sleeping processes

## 4. Process Management (`kernel/process.c`) — 478 lines
- Full lifecycle: create, exit, kill, wait, reap
- PID bitmap allocator (O(1))
- Process tree: parent/children/sibling links
- Signal delivery framework
- Security context (UID/GID/capabilities)

## 5. Synchronization (`kernel/sync.c`) — 483 lines
- Spinlocks with IRQ save/restore
- Mutexes with deadlock detection and priority inheritance
- Counting semaphores
- Reader-Writer locks
- Process barriers

## 6. Kernel Shell (`kernel/shell.c`) — 692 lines
- 28 commands: help, ps, kill, mem, dmesg, uptime, version, reboot, gpio,
  cpu, debug, clear, echo, sysctl, ls, cat, top, hexdump, uname, benchmark,
  net, crypto, **pen** (penetration testing arsenal)
- Command history, backspace, Ctrl-C/D handling
- ANSI color-coded prompt

## 7. TCP/IP Network Stack (`kernel/net/net.c`) — 1,147 lines
Complete from-scratch TCP/IP implementation:
- Ethernet II framing with VLAN
- ARP cache with timeout and gratuitous ARP
- IPv4 with fragmentation/reassembly, header checksum
- ICMP echo (ping), destination unreachable
- UDP sockets with multicast support
- **Full TCP state machine** (RFC 793+1122+5681):
  * Slow start, congestion avoidance, fast retransmit/recovery
  * SACK support, window scaling, timestamps
  * Keep-alive, Nagle's algorithm, RTT estimation
- DHCP client auto-configuration

## 8. Display / Framebuffer (`drivers/display.c`) — 446 lines
- MIPI DSI, LVDS, HDMI, DPI panel support
- Hardware-accelerated 2D compositing (VC4 HVS)
- Alpha blending, gamma correction, brightness control
- Hardware cursor (64x64 ARGB)
- vsync-based tear-free rendering
- Bitmap font text rendering

## 9. Touch Input (`drivers/touch.c`) — 482 lines
- Goodix GT911, FT5x06, Synaptics, Atmel mXT support
- 10-point multi-touch tracking
- Gesture recognition: tap, double-tap, long-press, swipe (4 directions),
  pinch, rotate
- Palm rejection and edge rejection
- Coordinate calibration
- Firmware update over I2C (IAP)

## 10. USB Host & Gadget (`drivers/usb.c`) — 596 lines
- Synopsys DWC3 dual-role controller
- **Host mode**: HID, Mass Storage (UASP/BOT), CDC-ACM, Audio, UVC
- **Gadget mode**: ADB, MTP, RNDIS tethering, UMS, MIDI, HID injection
- USB 2.0 HS + USB 3.0 SS
- OTG automatic role switching

## 11. WiFi 802.11 (`drivers/wifi.c`) — 416 lines
- SDIO/PCIe/USB WiFi modules (BCM43455, Intel AX200, RTL8822CE)
- 802.11a/b/g/n/ac/ax (WiFi 6)
- WPA3-SAE authentication
- **Monitor mode + packet injection** (pen testing)
- AP mode (software hotspot), Mesh networking (802.11s)
- Background scanning with roaming

## 12. Cellular Baseband (`drivers/cellular.c`) — 532 lines
- Qualcomm QMI, MediaTek AT+, Quectel EG25-G, SIMCom SIM7600
- **LTE Cat 20 / 5G NR SA/NSA**
- IMS voice (VoLTE/VoNR)
- SMS send/receive with PDU encoding/decoding
- USSD codes
- Voice calls: dial, answer, hangup, DTMF
- GPS/GNSS via modem
- Data: QMI/MBIM/AT data connection
- SIM detection, PIN/PUK handling
- Emergency calls (112/911)
- Signal strength, cell info, operator selection

## 13. Audio Subsystem (`drivers/audio.c`) — 445 lines
- I2S/PCM codec drivers (WM8960, ES8316, ALC5651)
- HDMI audio, USB audio, Bluetooth A2DP
- **7 simultaneous audio streams** with software mixing
- Per-stream volume, master volume
- Low-latency UI sound playback
- Call audio routing: handset, speaker, headset, Bluetooth
- Media audio ducking during calls

## 14. Camera / CSI (`drivers/camera.c`) — 595 lines
- MIPI CSI-2 with 4 data lanes @ 1.5 Gbps/lane
- OV5640, IMX258, S5K3L6, OV8858 sensor support
- Multiple streams: preview + capture + video simultaneously
- Hardware JPEG encoder with quality control
- H.264 hardware video encoder with bitrate control
- **Auto-Exposure, Auto-Focus, Auto-White-Balance** (10 Hz update)
- Contrast-based AF with macro/infinity/manual modes
- Flash/Torch LED control
- RAW10/RAW12 capture for pro mode

## 15. Power Management (`drivers/power.c`) — 658 lines
- **5 CPU governors**: performance, powersave, ondemand, conservative, interactive
- Interactive governor: touch-boost, hispeed freq, above-hispeed-delay
- 7 OPPs (600 MHz - 2.1 GHz) with voltage scaling
- CPU idle statistics, load tracking
- **Thermal management**: 4 trip points per zone, automatic throttling
- Battery fuel gauge (MAX17048): voltage, current, capacity, temp, health
- Charger detection (BC1.2, USB-PD)
- **Suspend-to-RAM** with wake sources (RTC, modem, WiFi, touch, USB)
- 15 power domains with individual sleep control
- Per-domain refcounting

## 16. CrimsonFS Filesystem (`kernel/fs/crfs.c`) — 719 lines
Log-structured filesystem for flash storage:
- Append-only log, copy-on-write for all writes
- Built-in LZ4 compression per block
- Per-file AES-256-GCM encryption
- CRC32C checksums for all metadata and data
- Snapshot support
- Inline data for small files (4KB)
- Triple-buffered directory reads
- Fast checkpoint recovery (no fsck)
- Full VFS interface: open/read/write/close/readdir/unlink/stat/sync

## 17. Cryptographic Engine (`kernel/crypto_stub.c`) — 203 lines
Framework for (full implementations to be integrated):
- AES-256-GCM authenticated encryption
- ChaCha20-Poly1305 stream cipher
- Ed25519 digital signatures
- SHA-256 / SHA-512 hashing
- HKDF-SHA256 key derivation
- Argon2id password hashing
- X25519 key exchange
- Hardware RNG support

## 18. Hardware Abstraction (`kernel/board.c`) — 201 lines
- Board detection: Raspberry Pi 4/3, QEMU, PinePhone, PinePhone Pro, Librem 5
- Device tree blob parsing
- CPU core enumeration
- Architecture context switch and process jump
- Stack trace on panic

## Headers — 29 files, ~1,800 lines
Complete type system covering every subsystem:
- types.h, memory.h, process.h, scheduler.h, spinlock.h, mutex.h, semaphore.h
- printk.h, version.h, string.h, asm.h, uart.h, gpio.h, timer.h, interrupt.h
- signal.h, driver.h, board.h, crypto.h, display.h, touch.h, usb.h, net.h
- wifi.h, cellular.h, audio.h, camera.h, power.h, crfs.h

---

## Architecture Summary

```
+------------------------------------------+
|           USER SPACE                      |
|  (Crimson Shell, BloodMoon Browser,      |
|   Phone App, Package Manager, etc.)       |
+--------------------+---------------------+
|     VFS Layer      |   System Calls      |
|   (crfs, devfs)    +---------------------+
+--------------------+   Process Manager    |
|   Network Stack    |   (fork/exec/exit)   |
|  (TCP/IP, UDP,     +---------------------+
|   DHCP, DNS)       |   Scheduler (MLFQ)  |
+--------------------+ +-------------------+
|   Display Server   | |  Memory Manager   |
|  (framebuffer,     | |  (PMM, VMM, Heap) |
|   compositor)      | +-------------------+
+--------------------+ |   Crypto Engine    |
|  Audio Mixer       | | (AES, Chacha20,   |
|  (7 streams,       | |  Ed25519, SHA)    |
|   call routing)    | +-------------------+
+--------------------+ |   Security (MAC,   |
|  Camera ISP        | |   capabilities)   |
|  (3A, JPEG, H.264) | +-------------------+
+--------------------+ +-------------------+
|  Driver Framework  | | Power Management  |
|  UART GPIO Timer   | | (DVFS, thermal,   |
|  GIC DSI CSI I2S   | |  battery, suspend)|
|  USB WiFi Cellular | +-------------------+
|  Audio Camera PM   | |   Boot Loader     |
+--------------------+ |   (MMU, exceptions,|
|     Crimson Core     |    context switch) |
|     Microkernel      +-------------------+
+--------------------+---------------------+
|              ARM64 Hardware              |
+------------------------------------------+
```
