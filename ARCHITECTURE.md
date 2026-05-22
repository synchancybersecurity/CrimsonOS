# Crimson OS Architecture Document

## System Overview

Crimson OS is built on **Crimson Core** — a custom microkernel designed specifically for mobile ARM64 devices. Unlike existing mobile operating systems that inherit decades of desktop-oriented design decisions, Crimson Core was designed from day one with mobile constraints in mind: battery life, thermal management, always-on connectivity, and touch-based interaction.

## Design Principles

1. **Security by Default** — Every subsystem is designed with security as the primary constraint
2. **Minimal Attack Surface** — Remove what isn't needed; complexity is the enemy of security
3. **User Sovereignty** — The user has ultimate control over their device
4. **Transparency** — All operations should be inspectable and auditable
5. **Performance for Responsiveness** — Prioritize UI responsiveness and real-time guarantees

## Kernel Architecture

### Boot Sequence

```
1. CPU reset vector -> boot.S:_start
2. Check exception level, drop to EL1 if needed
3. Set up stack pointer
4. Zero BSS section
5. Set up exception vectors (VBAR_EL1)
6. Initialize page tables (identity + higher half mapping)
7. Enable MMU and caches
8. Initialize GIC (interrupt controller)
9. Enable interrupts
10. Jump to kernel_main() in kmain.c
11. Initialize memory manager
12. Initialize interrupt subsystem
13. Initialize timer
14. Initialize device drivers
15. Initialize process management
16. Initialize scheduler
17. Initialize encryption subsystem
18. Create Crimson Shell (PID 1)
19. Create idle task
20. Enable interrupts, start scheduler
21. Scheduler context-switches to Shell
```

### Memory Layout

```
0x0000_0000_0000_0000 - 0x0000_7FFF_FFFF_FFFF: User space (128TB)
0x0000_8000_0000_0000 - 0xFFFF_7FFF_FFFF_FFFF: Unmapped
0xFFFF_0000_0000_0000 - 0xFFFF_7FFF_FFFF_FFFF: Kernel direct map
0xFFFF_8000_0000_0000 - 0xFFFF_FFFF_FFFF_FFFF: Kernel space

Physical Map:
0x00000 - 0x7FFFF: Reserved (bootloader, firmware)
0x80000 - 0x?????: Crimson OS kernel
  .text:  Executable code
  .rodata: Read-only data
  .data:   Initialized data
  .bss:    Uninitialized data
  Stack:   Kernel stack (1MB per CPU)
  Pagetbl: Page tables (4MB)
  Heap:    Dynamic allocation (256MB)
????? - end: Available for user space / drivers
```

### Context Switch

The context switch saves the following state:
- x19-x28 (callee-saved registers)
- x29 (frame pointer)
- x30 (link register / return address)
- SP (stack pointer)
- PC (program counter from ELR_EL1)
- PSTATE (processor state from SPSR_EL1)

Total: ~136 bytes per process context

Context switch latency target: <1 microsecond

### Scheduling Algorithm

**Multi-Level Feedback Queue (MLFQ)**

Five priority queues:
1. **REALTIME** (0): ISR-level tasks, never preempted
2. **HIGH** (1): UI rendering, audio, security checks (40ms quantum)
3. **NORMAL** (2): User applications, system services (80ms quantum)
4. **LOW** (3): Background sync, indexing (160ms quantum)
5. **IDLE** (4): Only runs when nothing else available (320ms quantum)

**Features:**
- O(1) task selection via bitmap
- Priority boosting every 2 seconds prevents starvation
- Automatic priority demotion for CPU hogs
- Per-CPU runqueues with load balancing

### Security Architecture

#### Capability-Based Access Control

Instead of traditional Unix permissions, Crimson OS uses a capability system:

```
Capability Bitmap (64 bits per process):
  [0]  CAP_ADMIN      - Full system access
  [1]  CAP_NET_RAW    - Raw network socket access
  [2]  CAP_NET_ADMIN  - Network configuration
  [3]  CAP_STORAGE    - Direct storage access
  [4]  CAP_DISPLAY    - Direct display/framebuffer access
  [5]  CAP_AUDIO      - Audio hardware access
  [6]  CAP_CAMERA     - Camera access
  [7]  CAP_GPS        - Location services
  [8]  CAP_BLUETOOTH  - Bluetooth control
  [9]  CAP_NFC        - NFC control
  [10] CAP_CELLULAR   - Cellular baseband access
  [11] CAP_SENSORS    - IMU, fingerprint, etc.
  [12] CAP_CRYPTO     - Hardware key store access
  [13] CAP_DEBUG      - Debug/tracing capabilities
  [14] CAP_TIME       - Set system time
  [15] CAP_PEN_TEST   - Penetration testing tools
```

#### Encryption Layers

```
Layer 1: Full Disk Encryption (AES-256-XTS)
  - Transparent to filesystem
  - Hardware-accelerated when available

Layer 2: Per-App Encryption (AES-256-GCM)
  - Each app has unique encryption keys
  - Data isolation between apps

Layer 3: Communication Encryption
  - All network traffic encrypted by default
  - BloodMoon browser: Tor + I2P + Clear net simultaneously
  - Message encryption: Signal Protocol implementation

Layer 4: Key Storage
  - Hardware-backed when available (TrustZone, TPM)
  - Software key store with Argon2id protection
  - Keys never leave secure storage unencrypted
```

### Driver Architecture

```
+------------------+
|  Driver Manager  |
+------------------+
       |
  +----+----+-------+--------+
  |         |       |        |
+---v-+  +--v--+ +--v---+ +--v----+
|Char |  |Block| |Network| |Display|
|Dev  |  |Dev  | |Dev   | |Dev   |
+------+ +-----+ +------+ +-------+
  |         |       |        |
+--v--------v-------v--------v-----+
|     Hardware Abstraction Layer    |
+-----------------------------------+
  |         |       |        |
+--v--+  +--v--+ +--v---+ +--v----+
|UART |  |SDHCI| |WiFi  | |DSI/LVDS
|GPIO  |  |USB  | |Cell  | |HDMI   |
+-----+  +-----+ +------+ +--------+
```

### Network Architecture

Crimson OS networking is designed for the **BloodMoon browser's multi-network capability**:

```
+------------------------+
|    BloodMoon Browser   |
+-----------+------------+
            |
  +---------+---------+---------+
  |         |         |         |
+--v--+  +--v--+  +--v--+  +--v--+
|Clear|  | Tor |  | I2P |  |Freenet
| Net |  |Net  |  |Net  |  |Net  |
+-----+  +-----+  +-----+  +-----+
  |         |         |         |
  +---------+---------+---------+
            |
+-----------v-------------+
|    Network Stack        |
| (TCP/IP, UDP, ICMP)     |
+-------------------------+
  |           |           |
+--v--+    +--v--+    +--v--+
|WiFi |    |Cell |    |Eth  |
+-----+    +-----+    +-----+
```

All network traffic is encrypted at the OS level. The browser can route different tabs through different networks simultaneously.

## Filesystem Design (Planned)

```
Crimson FS (crfs) - Log-structured filesystem:
- Copy-on-write for snapshots
- Built-in compression (LZ4)
- Per-file encryption
- Checksumming for all metadata and data
- Fast boot (no fsck needed)
```

## Power Management (Planned)

```
+----------------------------+
|    Power Manager           |
+------------+---------------+
             |
  +----------+----------+----------+
  |          |          |          |
+--v---+  +--v----+ +--v----+ +--v----+
|CPU   |  |Display| |Network| |Storage|
|DVFS  |  |Panel  | |Radios | |SD/eMMC|
+------+  +-------+ +-------+ +-------+
```

- Aggressive CPU idle (WFI/WFE)
- Display panel self-refresh
- Network batching for background tasks
- Storage power gating

## Development Guidelines

### Adding a New Driver

1. Create `drivers/<name>.c` and `include/crimson/<name>.h`
2. Implement `probe()`, `remove()`, and `shutdown()` functions
3. Register in `driver_register_all()`
4. Add to `Makefile` `DRV_SRCS`

### Adding a System Call

1. Define syscall number in `include/crimson/syscall.h`
2. Implement handler in `kernel/syscall.c`
3. Update userspace libc wrapper

### Porting to New Hardware

1. Add board definition to `include/crimson/board.h`
2. Create board init in `kernel/board.c`
3. Define peripheral base addresses
4. Provide device tree blob

## Performance Targets

| Metric | Target | Current |
|--------|--------|---------|
| Boot time (kernel) | <500ms | ~200ms (QEMU) |
| Context switch | <1us | ~500ns |
| Syscall latency | <500ns | Not measured |
| Interrupt latency | <10us | ~5us |
| Scheduler tick | 100Hz | 100Hz |
| Memory alloc (kmalloc) | <1us | ~2us |

## Testing Strategy

1. **Unit tests:** Each kernel subsystem has standalone tests
2. **Integration tests:** Full boot and shell interaction
3. **Hardware-in-the-loop:** Automated testing on real devices
4. **Fuzzing:** syscall interface and driver input validation
5. **Formal verification:** Critical security properties (planned)

---

*This document is a living specification. As Crimson OS evolves, so will this architecture.*
