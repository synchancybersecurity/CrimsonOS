/*
 * Crimson OS - Linker Script
 * ARM64 (AArch64) — QEMU virt machine
 *
 * Memory layout:
 *   0x40080000 : Kernel load address (QEMU virt default)
 *   .text      : Executable code
 *   .rodata    : Read-only data
 *   .data      : Initialized data
 *   .bss       : Uninitialized data (zeroed by boot.S)
 *   Stack      : 1MB kernel stack
 *   Page tables: 4MB
 *   Heap       : Remaining memory up to 256MB
 */

ENTRY(_start)

KERNEL_BASE = 0x40080000;

MEMORY
{
    RAM (rwx) : ORIGIN = 0x40080000, LENGTH = 256M
}

SECTIONS
{
    . = KERNEL_BASE;

    /* ── Boot code first — must be at load address ── */
    .boot : {
        *(.boot)
    } > RAM

    /* ── Executable code ── */
    .text ALIGN(4096) : {
        __text_start = .;
        *(.text .text.*)
        __text_end = .;
    } > RAM

    /* ── Read-only data ── */
    .rodata ALIGN(4096) : {
        __rodata_start = .;
        *(.rodata .rodata.*)
        __rodata_end = .;
    } > RAM

    /* ── Initialized data ── */
    .data ALIGN(4096) : {
        __data_start = .;
        *(.data .data.*)
        __data_end = .;
    } > RAM

    /* ── Uninitialized data (zeroed by boot.S) ── */
    .bss ALIGN(4096) : {
        __bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        __bss_end = .;
    } > RAM

    . = ALIGN(4096);

    /* ── Kernel size marker ── */
    __kernel_end = .;
    __kernel_size = __kernel_end - KERNEL_BASE;

    /* ── Kernel stack (1MB, grows downward) ── */
    . = ALIGN(16);
    __stack_bottom = .;
    . += 1M;
    __stack_top = .;

    /* ── Page tables (4MB aligned to 4KB) ── */
    . = ALIGN(4096);
    __page_tables_start = .;
    . += 4M;
    __page_tables_end = .;

    /* ── Kernel heap (everything remaining) ── */
    . = ALIGN(4096);
    __heap_start = .;
    __heap_end = ORIGIN(RAM) + LENGTH(RAM);

    /* ── Discard debug and comment sections ── */
    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
        *(.ARM.attributes)
    }
}
