# Crimson OS

## Independent Mobile Operating System

**Version:** 0.1.0-alpha "BloodMoon"  
**Architecture:** ARM64 (AArch64)  
**License:** GPLv3  
**Kernel:** Crimson Core v0.1.0

---

## The Manifesto

For decades, the mobile operating system landscape has been a duopoly. Two companies control the software that runs on billions of devices. Your phone — your most personal computer — runs code you cannot inspect, cannot modify, and cannot trust. Every app you install, every message you send, every location you visit is filtered through corporate-controlled infrastructure designed to extract value from your data.

**Crimson OS exists to change this.**

We believe:
- **Privacy is a fundamental human right**, not a premium feature
- **Security should be the default**, not an afterthought
- **Users should control their devices**, not the other way around
- **The code that runs your phone should be inspectable, auditable, and modifiable**
- **Penetration testing and security research tools should be native capabilities**, not root-requiring workarounds
- **Encrypted communication should be the standard**, not the exception
- **A third option should exist** — built from the ground up for security, privacy, and user freedom

Crimson OS is that third option.

---

## What is Crimson OS?

Crimson OS is a **from-scratch mobile operating system** built on an entirely original kernel (Crimson Core). It is:

- **NOT Android** — no Google services, no Android runtime, no Linux kernel
- **NOT iOS** — no Apple ecosystem, no walled garden
- **NOT a Linux distribution** — built on a custom microkernel with our own drivers, scheduler, and memory manager

Crimson OS provides **Linux-level control** over your device while being a completely independent codebase — no shared lineage with existing mobile operating systems.

### Key Differentiators

| Feature | Crimson OS | Android | iOS |
|---------|-----------|---------|-----|
| Open Source | 100% GPLv3 | Partial | No |
| Kernel | Crimson Core (original) | Linux | XNU (BSD) |
| Pen Testing | Native arsenal | Root required | Jailbreak required |
| Encrypted Comms | Native, default | Apps only | iMessage only |
| Tor Support | Native (BloodMoon browser) | Orbot app | Onion Browser app |
| User Control | Full | Limited (root) | None |
| Trackers | Zero built-in | Google Play Services | Apple services |
| Hardware Flex | Multi-device flashable | Vendor-locked | Device-locked |

---

## Architecture

### Crimson Core Kernel

```
+-------------------------+
|     User Space          |
|  (Crimson Shell, Apps)  |
+------------+------------+
|  System Call Interface  |
+------------+------------+
|   Process Management    |
|   (Scheduler, IPC)      |
+------------+------------+
|   Memory Management     |
|   (PMM, VMM, kmalloc)   |
+------------+------------+
|   Security Layer        |
|   (Crypto, Capabilities)|
+------------+------------+
|   Driver Framework      |
|   (UART, GPIO, Timer,   |
|    GIC, Display, Net)   |
+------------+------------+
|   Crimson Core Kernel   |
|   (boot.S, Exceptions)  |
+------------+------------+
|      ARM64 Hardware     |
+-------------------------+
```

### Kernel Components

#### Memory Management
- **Physical Memory Manager (PMM):** Bitmap-based page frame allocator, O(1) allocation
- **Kernel Heap:** Best-fit allocator with coalescing
- **Virtual Memory Manager (VMM):** 4-level page tables, demand paging support

#### Process Management
- **Multi-Level Feedback Queue (MLFQ) scheduler** with 5 priority levels
- **Priority boosting** to prevent starvation
- **Preemptive multitasking** at 100Hz tick rate
- **Wait queues** for sleeping processes
- **Full process lifecycle:** create, fork, exec, wait, exit, kill

#### Synchronization
- **Spinlocks** with interrupt management
- **Mutexes** with priority inheritance and deadlock detection
- **Semaphores** (counting)
- **Reader-Writer locks**
- **Process barriers**

#### Device Drivers
- **UART PL011:** Serial console, 115200 baud
- **GPIO:** Full pin control with pull-up/down, alt functions, interrupts
- **ARM Generic Timer:** High-resolution timing, periodic scheduling
- **GICv2:** Full interrupt controller with SGI/PPI/SPI support

#### Security (Planned/Stubbed)
- AES-256-GCM authenticated encryption
- ChaCha20-Poly1305 stream cipher
- Ed25519 digital signatures
- SHA-256 / SHA-512 hashing
- HKDF-SHA256 key derivation
- Argon2id password hashing
- X25519 key exchange
- Hardware RNG support

### BloodMoon Browser

The native Crimson OS web browser supports **simultaneous multi-network access**:
- **Clear Net:** Standard internet browsing
- **Tor Network:** .onion sites, anonymous browsing
- **I2P Network:** Garlic routing for peer-to-peer applications
- **Freenet:** Distributed data storage access

All traffic through BloodMoon is encrypted at the OS level with per-session keys.

### Crimson Package Repository

An open-source app store with:
- Cryptographically signed packages
- Reproducible builds
- Source code availability for all packages
- Security audit trail for every package
- Penetration testing tools pre-curated and maintained

---

## Hardware Targets

Crimson OS targets ARM64 mobile hardware. Supported (or planned) devices:

| Device | Status | Notes |
|--------|--------|-------|
| QEMU ARM64 Virt | **Bootable** | Primary development target |
| Raspberry Pi 4 | In Progress | GPIO, UART verified |
| PinePhone (A64) | Planned | Open hardware, ideal target |
| PinePhone Pro (RK3399S) | Planned | More powerful, 6 cores |
| Librem 5 (i.MX8MQ) | Planned | Privacy-focused hardware |
| OnePlus 6/6T | Planned | Snapdragon 845 |
| Google Pixel 3a | Planned | Well-documented bootloader |

### Porting to New Hardware

Crimson OS uses a Hardware Abstraction Layer (HAL) that makes porting straightforward:

1. Create a board definition in `include/crimson/board.h`
2. Implement platform-specific GPIO/UART base addresses
3. Provide a device tree blob (DTB) for the target
4. Configure the bootloader chain

Most ports require only a few hundred lines of platform-specific code.

---

## Building Crimson OS

### Prerequisites

```bash
# Ubuntu/Debian
sudo apt-get install gcc-aarch64-linux-gnu binutils-aarch64-linux-gnu qemu-system-arm

# macOS (with Homebrew)
brew install aarch64-elf-gcc qemu

# Arch Linux
sudo pacman -S aarch64-linux-gnu-gcc qemu-system-aarch64
```

### Build

```bash
cd crimson-os

# Build for QEMU (default)
make

# Build for Raspberry Pi 4
make BOARD=rpi4

# Build for QEMU (explicit)
make BOARD=qemu
```

### Run in QEMU

```bash
# Run the OS
make qemu

# Debug mode (waits for GDB on port 1234)
make qemu-debug
```

### Flash to Hardware (Raspberry Pi 4)

```bash
# Build RPi4 image
make BOARD=rpi4

# Write to SD card (replace sdX with your device)
sudo dd if=bin/crimson-os.img of=/dev/sdX bs=512 seek=2048 status=progress

# Or copy to boot partition
sudo cp bin/crimson-os.img /mnt/boot/kernel8.img
```

---

## The Crimson Shell

The kernel boots into the Crimson Shell — your command center for the OS:

```
  PID  PPID  STATE   CPU   PRIO    TIME  NAME
  ---  ----  -----   ---   ----    ----  ----
    1     0  RUN     0    HIGH      42  crimson-shell

╔══════════════════════════════════════════════════════════════╗
║                 CRIMSON SHELL COMMANDS                       ║
╠══════════════════════════════════════════════════════════════╣
║  help        Show available commands                         ║
║  ps          List running processes                          ║
║  kill        Send signal to process                          ║
║  mem         Show memory information                         ║
║  dmesg       Display kernel log                              ║
║  uptime      Show system uptime                              ║
║  version     Show OS version                                 ║
║  reboot      Reboot the system                               ║
║  gpio        Control GPIO pins                               ║
║  cpu         Show CPU information                            ║
║  debug       Debug utilities                                 ║
║  clear       Clear screen                                    ║
║  echo        Print text                                      ║
║  sysctl      System parameters                               ║
║  ls          List files/devices                              ║
║  cat         Display file/device                             ║
║  top         Show system processes                           ║
║  hexdump     Hexdump memory region                           ║
║  uname       Print system information                        ║
║  benchmark   Run system benchmarks                           ║
║  net         Network configuration                           ║
║  crypto      Cryptographic tools                             ║
║  pen         Penetration testing arsenal                     ║
╚══════════════════════════════════════════════════════════════╝
```

### Penetration Testing Arsenal

Crimson OS includes (or will include via the package manager) a comprehensive security toolkit:

- **Network:** nmap, tcpdump, wireshark, netcat, aircrack-ng
- **Wireless:** kismet, reaver, wifite
- **Bluetooth:** bluez stack, blueranger
- **RFID/NFC:** proxmark3, mfoc
- **SDR:** rtl-sdr, hackrf
- **Exploitation:** metasploit, sqlmap, nikto
- **Forensics:** sleuthkit, autopsy, foremost
- **Crypto/Analysis:** john, hashcat, openssl
- **Hardware Hacking:** flashrom, openocd, buspirate

---

## Project Structure

```
crimson-os/
├── boot/               # Bootloader and CPU initialization
│   └── boot.S         # ARM64 boot entry, exception vectors, context switch
├── kernel/            # Crimson Core kernel
│   ├── kmain.c        # Kernel main entry, initialization
│   ├── mm.c           # Memory manager (PMM, VMM, kmalloc)
│   ├── sched.c        # MLFQ process scheduler
│   ├── process.c      # Process management
│   ├── printk.c       # Kernel logging and ring buffer
│   ├── sync.c         # Synchronization primitives
│   ├── shell.c        # Crimson Shell command interpreter
│   ├── board.c        # Board detection and platform setup
│   ├── driver.c       # Driver registration framework
│   ├── crypto_stub.c  # Cryptographic subsystem (stubs)
│   └── vmm_stub.c     # VMM helper (stubs)
├── drivers/           # Device drivers
│   ├── uart.c         # PL011 serial driver
│   ├── gpio.c         # GPIO pin controller
│   ├── timer.c        # ARM Generic Timer
│   └── gic.c          # GICv2 interrupt controller
├── lib/               # Kernel library
│   ├── string.c       # memset, memcpy, strcmp, etc.
│   └── stdlib.c       # atoi, strtoul, itoa
├── include/           # Kernel headers
│   └── crimson/       # All subsystem headers
├── docs/              # Documentation (roadmap, architecture)
├── tools/             # Build and flashing tools
├── Makefile           # Build system
├── linker.ld          # Kernel linker script
└── README.md          # This file
```

---

## Roadmap

### Phase 1: Crimson Core (CURRENT - v0.1.x)
- [x] ARM64 bootloader and exception handling
- [x] Physical memory management
- [x] Virtual memory with page tables
- [x] Process scheduler (MLFQ)
- [x] Process lifecycle management
- [x] UART serial console
- [x] GPIO control
- [x] ARM Generic Timer
- [x] GICv2 interrupt controller
- [x] Synchronization primitives
- [x] Crimson Shell
- [ ] Display/framebuffer driver
- [ ] USB host controller
- [ ] SDHCI (SD card) driver
- [ ] Basic filesystem (VFS layer)

### Phase 2: User Land (v0.2.x)
- [ ] ELF loader for user programs
- [ ] System call interface
- [ ] Dynamic memory allocator (user space)
- [ ] POSIX-compatible layer
- [ ] Dynamic linker
- [ ] Userspace shell
- [ ] File system (ext4 or custom)

### Phase 3: Networking (v0.3.x)
- [ ] Network stack (TCP/IP)
- [ ] WiFi driver framework
- [ ] Cellular baseband interface
- [ ] Bluetooth stack
- [ ] BloodMoon browser engine

### Phase 4: Security (v0.4.x)
- [ ] Full cryptographic implementations
- [ ] Secure boot chain
- [ ] Verified boot
- [ ] SELinux-style MAC
- [ ] Full disk encryption
- [ ] Tor integration
- [ ] I2P integration

### Phase 5: Mobile (v0.5.x)
- [ ] Display server (Wayland-like)
- [ ] Touch input framework
- [ ] Phone app (calls/SMS)
- [ ] Contacts manager
- [ ] Camera framework
- [ ] Audio subsystem
- [ ] Power management
- [ ] Package manager (Crimson Store)

### Phase 6: Release (v1.0.0)
- [ ] Multi-device support (10+ phones)
- [ ] Full pen testing toolkit
- [ ] Encrypted communications suite
- [ ] BloodMoon browser (Tor/Clear/I2P)
- [ ] Stable daily driver quality

---

## Contributing

Crimson OS is a community project. We need:

- **Kernel developers** — ARM64 assembly, memory management, scheduling
- **Driver developers** — USB, WiFi, cellular, display, audio
- **Security researchers** — cryptography, exploit mitigation, auditing
- **Mobile developers** — UI framework, phone apps, power management
- **Hardware porters** — bring Crimson OS to new devices
- **Testers** — run on real hardware, report bugs

### Code Standards

- All code must compile with `-Wall -Wextra`
- No external runtime dependencies (kernel is self-contained)
- Comment complex algorithms
- Follow existing code style
- Security-first: validate all inputs, minimize attack surface

---

## Security Disclosure

We take security seriously. If you discover a vulnerability:

1. Email security@crimson-os.org
2. Do NOT open a public issue
3. Allow 90 days for remediation before public disclosure
4. Credit will be given in the security advisory

---

## License

Crimson OS is licensed under the **GNU General Public License v3.0**.

```
This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
```

**Our goal:** A fully open, auditable, community-controlled mobile operating system that puts users first. Every line of code should be inspectable. Every feature should serve the user, not a corporation. Every device should be truly yours.

**Join the revolution. Break the duopoly. Choose Crimson.**

---

## Contact

- Website: https://crimson-os.org (coming soon)
- Matrix: #crimson-os:matrix.org (coming soon)
- Mastodon: @CrimsonOS@fosstodon.org (coming soon)
- IRC: #crimson-os on Libera.Chat (coming soon)

*Built with defiance. Designed for freedom. Powered by community.*
