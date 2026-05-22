/*
 * Crimson OS - Crimson Shell
 * 
 * The primary command-line interface for Crimson OS.
 * Provides system administration, debugging, process control,
 * and system monitoring capabilities.
 * 
 * Commands:
 *   help, ps, kill, mem, dmesg, reboot, uptime, cpu, gpio,
 *   net, crypto, version, debug, mount, ls, cat, echo, clear
 * 
 * The shell is PID 1 (init) and is always available via serial console.
 */

#include <crimson/types.h>
#include <crimson/printk.h>
#include <crimson/process.h>
#include <crimson/scheduler.h>
#include <crimson/memory.h>
#include <crimson/timer.h>
#include <crimson/uart.h>
#include <crimson/version.h>
#include <crimson/string.h>
#include <crimson/pkg.h>
#include <crimson/security.h>
#include <crimson/gpio.h>
#include <crimson/mm.h>
#include <crimson/fs.h>
#include <crimson/crfs.h>

#define SHELL_PROMPT    "\n\x1B[31mcrimson\x1B[37m:\x1B[36m~\x1B[37m# "
#define SHELL_MAX_LINE  256
#define SHELL_MAX_ARGS  16
#define SHELL_HISTORY   16

/* Shell state */
static char shell_buffer[SHELL_MAX_LINE];
static char* shell_argv[SHELL_MAX_ARGS];
static int shell_argc = 0;
static bool shell_running = true;

/* History buffer */
static char shell_history[SHELL_HISTORY][SHELL_MAX_LINE];
static int shell_history_count = 0;
static int shell_history_idx = 0;

/* Forward declarations */
static void shell_read_line(void);
static int shell_parse_args(char* line);
static void shell_execute(void);
static void shell_print_prompt(void);

/* Command handlers */
static void cmd_help(void);
static void cmd_ps(void);
static void cmd_kill(void);
static void cmd_mem(void);
static void cmd_dmesg(void);
static void cmd_uptime(void);
static void cmd_version(void);
static void cmd_reboot(void);
static void cmd_gpio(void);
static void cmd_cpu(void);
static void cmd_debug(void);
static void cmd_clear(void);
static void cmd_echo(void);
static void cmd_sysctl(void);
static void cmd_ls(void);
static void cmd_cat(void);
static void cmd_top(void);
static void cmd_hexdump(void);
static void cmd_uname(void);
static void cmd_whoami(void);
static void cmd_date(void);
static void cmd_mount(void);
static void cmd_benchmark(void);
static void cmd_net(void);
static void cmd_crypto(void);
static void cmd_pen(void);
static void cmd_pkg(void);
static void cmd_sec(void);

/* Command table */
typedef struct {
    const char* name;
    const char* description;
    const char* usage;
    void (*handler)(void);
} shell_cmd_t;

static const shell_cmd_t commands[] = {
    { "help",      "Show available commands",              "help [command]",         cmd_help },
    { "?",         "Alias for help",                       "?",                      cmd_help },
    { "ps",        "List running processes",               "ps",                     cmd_ps },
    { "kill",      "Send signal to process",               "kill <pid> [signal]",    cmd_kill },
    { "mem",       "Show memory information",              "mem",                    cmd_mem },
    { "dmesg",     "Display kernel log",                   "dmesg [-c]",             cmd_dmesg },
    { "uptime",    "Show system uptime",                   "uptime",                 cmd_uptime },
    { "version",   "Show OS version",                      "version",                cmd_version },
    { "reboot",    "Reboot the system",                    "reboot",                 cmd_reboot },
    { "gpio",      "Control GPIO pins",                    "gpio <pin> <in|out|high|low|read>", cmd_gpio },
    { "cpu",       "Show CPU information",                 "cpu",                    cmd_cpu },
    { "debug",     "Debug utilities",                      "debug <subcommand>",     cmd_debug },
    { "clear",     "Clear screen",                         "clear",                  cmd_clear },
    { "echo",      "Print text",                           "echo [text...]",         cmd_echo },
    { "sysctl",    "System parameters",                    "sysctl <get|set> <param>", cmd_sysctl },
    { "ls",        "List files/devices",                   "ls [path]",              cmd_ls },
    { "cat",       "Display file/device",                  "cat <path>",             cmd_cat },
    { "top",       "Show system processes (live)",         "top",                    cmd_top },
    { "hexdump",   "Hexdump memory region",                "hexdump <addr> [len]",   cmd_hexdump },
    { "uname",     "Print system information",             "uname [-a]",             cmd_uname },
    { "whoami",    "Print current user",                   "whoami",                 cmd_whoami },
    { "date",      "Print system date/time",               "date",                   cmd_date },
    { "mount",     "Show mounted filesystems",             "mount",                  cmd_mount },
    { "benchmark", "Run system benchmarks",                "benchmark [mem|cpu]",    cmd_benchmark },
    { "net",       "Network configuration",                "net <subcommand>",       cmd_net },
    { "crypto",    "Cryptographic tools",                  "crypto <subcommand>",    cmd_crypto },
    { "pen",       "Penetration testing tools",            "pen <subcommand>",       cmd_pen },
    { "pkg",       "Package manager",                      "pkg <subcommand>",       cmd_pkg },
    { "sec",       "Security controls",                    "sec <subcommand>",       cmd_sec },
    { NULL, NULL, NULL, NULL }
};

/*
 * shell_run - Main shell loop
 */
void shell_run(void)
{
    printk("\nWelcome to Crimson Shell v0.1\n");
    printk("Type 'help' for available commands.\n\n");
    
    while (shell_running) {
        shell_print_prompt();
        shell_read_line();
        
        if (shell_buffer[0] != '\0') {
            shell_execute();
        }
    }
}

/*
 * shell_print_prompt - Print the shell prompt
 */
static void shell_print_prompt(void)
{
    uart_puts(SHELL_PROMPT);
}

/*
 * shell_read_line - Read a line from UART
 */
static void shell_read_line(void)
{
    int pos = 0;
    char c;
    
    while (1) {
        c = uart_getc_blocking();
        
        /* Handle special characters */
        if (c == '\r' || c == '\n') {
            shell_buffer[pos] = '\0';
            uart_putc('\n');
            return;
        }
        else if (c == '\b' || c == 0x7F) {
            /* Backspace */
            if (pos > 0) {
                pos--;
                uart_puts("\b \b");
            }
        }
        else if (c == 0x03) {
            /* Ctrl-C */
            shell_buffer[0] = '\0';
            uart_putc('\n');
            return;
        }
        else if (c == 0x04) {
            /* Ctrl-D */
            if (pos == 0) {
                shell_running = false;
                return;
            }
        }
        else if ((unsigned char)c >= ' ' && pos < SHELL_MAX_LINE - 1) {
            /* Printable character */
            shell_buffer[pos++] = c;
            uart_putc(c);
        }
    }
}

/*
 * shell_parse_args - Parse line into argc/argv
 */
static int shell_parse_args(char* line)
{
    int argc = 0;
    char* p = line;
    
    while (*p && argc < SHELL_MAX_ARGS) {
        /* Skip whitespace */
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        
        /* Find end of argument */
        char* start = p;
        while (*p && *p != ' ' && *p != '\t') p++;
        
        if (*p) *p++ = '\0';
        shell_argv[argc++] = start;
    }
    
    shell_argc = argc;
    return argc;
}

/*
 * shell_execute - Execute parsed command
 */
static void shell_execute(void)
{
    /* Store in history */
    strncpy(shell_history[shell_history_idx], shell_buffer, SHELL_MAX_LINE - 1);
    shell_history_idx = (shell_history_idx + 1) % SHELL_HISTORY;
    if (shell_history_count < SHELL_HISTORY) shell_history_count++;
    
    /* Parse arguments */
    shell_parse_args(shell_buffer);
    
    if (shell_argc == 0) return;
    
    /* Find and execute command */
    for (const shell_cmd_t* cmd = commands; cmd->name != NULL; cmd++) {
        if (strcmp(shell_argv[0], cmd->name) == 0) {
            cmd->handler();
            return;
        }
    }
    
    printk("crimson: command not found: '%s'\n", shell_argv[0]);
    printk("Type 'help' for available commands.\n");
}

/* ─── Command Handlers ─── */

static void cmd_help(void)
{
    if (shell_argc > 1) {
        /* Help for specific command */
        for (const shell_cmd_t* cmd = commands; cmd->name != NULL; cmd++) {
            if (strcmp(shell_argv[1], cmd->name) == 0) {
                printk("Command: %s\n", cmd->name);
                printk("Usage:   %s\n", cmd->usage);
                printk("%s\n", cmd->description);
                return;
            }
        }
        printk("Unknown command: '%s'\n", shell_argv[1]);
        return;
    }
    
    printk("\n╔══════════════════════════════════════════════════════════════╗\n");
    printk("║                 CRIMSON SHELL COMMANDS                       ║\n");
    printk("╠══════════════════════════════════════════════════════════════╣\n");
    
    for (const shell_cmd_t* cmd = commands; cmd->name != NULL; cmd++) {
        printk("║  %-10s %-45s  ║\n", cmd->name, cmd->description);
    }
    
    printk("╚══════════════════════════════════════════════════════════════╝\n\n");
}

static void cmd_ps(void)
{
    process_list();
}

static void cmd_kill(void)
{
    if (shell_argc < 2) {
        printk("Usage: kill <pid> [signal]\n");
        return;
    }
    
    pid_t pid = atoi(shell_argv[1]);
    int sig = (shell_argc > 2) ? atoi(shell_argv[2]) : 15;  /* Default SIGTERM */
    
    int result = process_kill(pid, sig);
    if (result == STATUS_OK) {
        printk("Sent signal %d to PID %d\n", sig, pid);
    } else {
        printk("kill: failed to send signal to PID %d (error %d)\n", pid, result);
    }
}

static void cmd_mem(void)
{
    extern size_t pmm_get_free_pages(void);
    extern size_t total_pages;
    
    size_t free_pages = pmm_get_free_pages();
    size_t free_kb = free_pages * PAGE_SIZE / 1024;
    size_t total_kb = total_pages * PAGE_SIZE / 1024;
    
    printk("\n=== Memory Information ===\n");
    printk("Total pages:  %lu (%lu KB / %lu MB)\n", total_pages, total_kb, total_kb / 1024);
    printk("Free pages:   %lu (%lu KB / %lu MB)\n", free_pages, free_kb, free_kb / 1024);
    printk("Used pages:   %lu (%lu KB / %lu MB)\n", total_pages - free_pages,
           (total_pages - free_pages) * PAGE_SIZE / 1024,
           (total_pages - free_pages) * PAGE_SIZE / 1024 / 1024);
    printk("Page size:    %d KB\n", PAGE_SIZE / 1024);
    printk("==========================\n\n");
}

static void cmd_dmesg(void)
{
    bool clear_log = false;
    
    if (shell_argc > 1 && strcmp(shell_argv[1], "-c") == 0) {
        clear_log = true;
    }
    
    extern size_t dmesg(char* buf, size_t size);
    
    char* buf = kmalloc(4096);
    if (!buf) {
        printk("dmesg: out of memory\n");
        return;
    }
    
    size_t len = dmesg(buf, 4096);
    if (len > 0) {
        printk("\n--- Kernel Log ---\n");
        uart_write(buf, len);
        printk("\n--- End of Log ---\n\n");
    } else {
        printk("dmesg: log buffer empty\n");
    }
    
    kfree(buf);
    
    if (clear_log) {
        extern void dmesg_clear(void);
        dmesg_clear();
        printk("Kernel log cleared.\n");
    }
}

static void cmd_uptime(void)
{
    uint64_t ms = timer_get_uptime_ms();
    uint64_t secs = ms / 1000;
    uint64_t mins = secs / 60;
    uint64_t hours = mins / 60;
    uint64_t days = hours / 24;
    
    printk("Uptime: ");
    if (days > 0) printk("%lu days, ", days);
    printk("%02lu:%02lu:%02lu\n", hours % 24, mins % 60, secs % 60);
    printk("(%lu milliseconds since boot)\n", ms);
}

static void cmd_version(void)
{
    printk("\n");
    printk("Crimson OS v%s \"%s\"\n", CRIMSON_VERSION, CRIMSON_CODENAME);
    printk("Build: %s %s\n", __DATE__, __TIME__);
    printk("Architecture: ARM64 (AArch64)\n");
    printk("Kernel: Crimson Core v0.1.0\n");
    printk("Compiler: GCC\n");
    printk("\n");
    printk("Crimson OS is free software released under GPLv3.\n");
    printk("https://crimson-os.org\n");
    printk("\n");
}

static void cmd_reboot(void)
{
    printk("System rebooting...\n");
    uart_flush();
    
    /* ARM PSCI reboot */
    __asm__ volatile(
        "ldr x0, =0x84000009\n"   /* PSCI_SYSTEM_RESET */
        "hvc #0\n"
    );
    
    /* If PSCI fails, try watchdog */
    while (1) {
        __asm__ volatile("wfi");
    }
}

static void cmd_gpio(void)
{
    if (shell_argc < 3) {
        printk("Usage: gpio <pin> <in|out|high|low|read|toggle>\n");
        return;
    }
    
    uint32_t pin = atoi(shell_argv[1]);
    
    if (strcmp(shell_argv[2], "in") == 0) {
        gpio_set_input(pin);
        printk("GPIO %d configured as input\n", pin);
    }
    else if (strcmp(shell_argv[2], "out") == 0) {
        gpio_set_output(pin);
        printk("GPIO %d configured as output\n", pin);
    }
    else if (strcmp(shell_argv[2], "high") == 0) {
        gpio_write(pin, 1);
        printk("GPIO %d set HIGH\n", pin);
    }
    else if (strcmp(shell_argv[2], "low") == 0) {
        gpio_write(pin, 0);
        printk("GPIO %d set LOW\n", pin);
    }
    else if (strcmp(shell_argv[2], "read") == 0) {
        int val = gpio_read(pin);
        printk("GPIO %d = %d\n", pin, val);
    }
    else if (strcmp(shell_argv[2], "toggle") == 0) {
        gpio_toggle(pin);
        printk("GPIO %d toggled\n", pin);
    }
    else {
        printk("Unknown GPIO command: '%s'\n", shell_argv[2]);
    }
}

static void cmd_cpu(void)
{
    extern uint32_t cpu_get_core_count(void);
    extern uint32_t cpu_get_id(void);
    
    printk("\n=== CPU Information ===\n");
    printk("Core count: %d\n", cpu_get_core_count());
    printk("Current core: %d\n", cpu_get_id());
    printk("Architecture: ARM64 (AArch64)\n");
    printk("Timer frequency: %u Hz\n", timer_get_frequency());
    printk("=======================\n\n");
}

static void cmd_debug(void)
{
    if (shell_argc < 2) {
        printk("Usage: debug <scheduler|gic|uart|memory>\n");
        return;
    }
    
    if (strcmp(shell_argv[1], "scheduler") == 0) {
        extern void scheduler_stats(void);
        scheduler_stats();
    }
    else if (strcmp(shell_argv[1], "uart") == 0) {
        extern void uart_debug_dump(void);
        uart_debug_dump();
    }
    else {
        printk("Unknown debug target: '%s'\n", shell_argv[1]);
    }
}

static void cmd_clear(void)
{
    /* ANSI escape sequence to clear screen */
    printk("\x1B[2J\x1B[H");
}

static void cmd_echo(void)
{
    for (int i = 1; i < shell_argc; i++) {
        if (i > 1) printk(" ");
        printk("%s", shell_argv[i]);
    }
    printk("\n");
}

static void cmd_sysctl(void)
{
    if (shell_argc < 2) {
        printk("Usage: sysctl <get|set> <parameter> [value]\n");
        return;
    }
    
    if (strcmp(shell_argv[1], "get") == 0 && shell_argc > 2) {
        if (strcmp(shell_argv[2], "loglevel") == 0) {
            extern int printk_get_level(void);
            printk("loglevel = %d\n", printk_get_level());
        }
        else {
            printk("Unknown parameter: '%s'\n", shell_argv[2]);
        }
    }
    else if (strcmp(shell_argv[1], "set") == 0 && shell_argc > 3) {
        if (strcmp(shell_argv[2], "loglevel") == 0) {
            extern void printk_set_level(int);
            int level = atoi(shell_argv[3]);
            printk_set_level(level);
            printk("loglevel set to %d\n", level);
        }
    }
}

static void cmd_ls(void)
{
    const char* path = (shell_argc > 1) ? shell_argv[1] : "/";

    /* Stat the target to determine type */
    fs_stat_t st;
    if (fs_stat(path, &st) < 0) {
        /* CrimsonFS may not be mounted yet — fall back to device listing */
        if (shell_argc <= 1) {
            printk("\nVirtual devices:\n");
            printk("  /dev/uart0     serial console\n");
            printk("  /dev/null      null sink\n");
            printk("  /dev/zero      zero source\n");
            printk("  /dev/random    CSPRNG\n");
            printk("  /dev/mem       physical memory\n");
            printk("  /dev/gpio      GPIO interface\n");
        } else {
            printk("ls: %s: no such file or directory\n", path);
        }
        return;
    }

    if (st.type != FS_TYPE_DIR) {
        /* Single file — print stat info */
        printk("%s  %lu bytes  mode=%04o\n", path, (unsigned long)st.size, st.mode);
        return;
    }

    /* Directory: open in CrimsonFS and read raw directory blocks */
    int fd = crfs_open(path, O_RDONLY, 0);
    if (fd < 0) {
        printk("ls: cannot open %s\n", path);
        return;
    }

    /* Read up to one full block of directory data */
    uint8_t blk[4096];
    int64_t n = crfs_read(fd, blk, sizeof(blk));
    crfs_close(fd);

    if (n <= 0) {
        printk("%s: (empty)\n", path);
        return;
    }

    printk("\n%s:\n", path);

    uint32_t off = 0;
    int entry_count = 0;
    while (off + sizeof(crfs_dirent_t) <= (uint32_t)n) {
        crfs_dirent_t* de = (crfs_dirent_t*)(blk + off);
        if (de->inode_no == 0 || de->rec_len == 0) break;

        /* Name immediately follows the fixed header in the block */
        char name[256];
        uint8_t nlen = de->name_len < 255 ? de->name_len : 255;
        memcpy(name, blk + off + sizeof(crfs_dirent_t), nlen);
        name[nlen] = '\0';

        char type_c = (de->type == 2) ? 'd' : '-';
        printk("  %c  %s\n", type_c, name);
        entry_count++;

        off += de->rec_len;
    }

    if (entry_count == 0)
        printk("  (empty directory)\n");
    printk("\n");
}

static void cmd_cat(void)
{
    if (shell_argc < 2) {
        printk("Usage: cat <path>\n");
        return;
    }

    const char* path = shell_argv[1];

    /* Synthetic device paths */
    if (strcmp(path, "/dev/version") == 0) { cmd_version(); return; }

    /* Open via VFS */
    int fd = fs_open(path, FS_O_RDONLY);
    if (fd < 0) {
        printk("cat: %s: no such file or directory\n", path);
        return;
    }

    /* Read and print in 512-byte chunks */
    char buf[512];
    ssize_t n;
    while ((n = fs_read(fd, buf, sizeof(buf) - 1)) > 0) {
        buf[n] = '\0';
        uart_write(buf, (size_t)n);
    }
    if (n < 0)
        printk("\ncat: read error\n");
    else
        printk("\n");   /* trailing newline after file content */

    fs_close(fd);
}

static void cmd_top(void)
{
    cmd_ps();
    printk("\nNote: Live top mode not yet implemented.\n");
}

static void cmd_hexdump(void)
{
    if (shell_argc < 2) {
        printk("Usage: hexdump <address> [length]\n");
        return;
    }
    
    uintptr_t addr = strtoul(shell_argv[1], NULL, 0);
    size_t len = (shell_argc > 2) ? strtoul(shell_argv[2], NULL, 0) : 256;
    
    if (len > 4096) len = 4096;  /* Limit for safety */
    
    extern void printk_hexdump(const void* addr, size_t len);
    printk_hexdump((const void*)addr, len);
}

static void cmd_uname(void)
{
    bool all = (shell_argc > 1 && strcmp(shell_argv[1], "-a") == 0);
    
    if (all) {
        printk("CrimsonOS %s crimson arm64 CrimsonOS/%s\n",
               CRIMSON_VERSION, CRIMSON_CODENAME);
    } else {
        printk("CrimsonOS\n");
    }
}

static void cmd_whoami(void)
{
    printk("root\n");
}

static void cmd_date(void)
{
    uint64_t ms = timer_get_uptime_ms();
    uint64_t secs = ms / 1000;
    uint64_t mins = secs / 60;
    uint64_t hours = mins / 60;
    
    /* Simple formatted time since boot */
    printk("%02lu:%02lu:%02lu.%03lu (since boot)\n",
           hours % 24, mins % 60, secs % 60, ms % 1000);
}

static void cmd_mount(void)
{
    printk("\n=== Mounted Filesystems ===\n");
    printk("Device       Mount Point    Type    Options\n");
    printk("(No filesystems mounted yet)\n");
    printk("===========================\n\n");
}

static void cmd_benchmark(void)
{
    printk("\n=== System Benchmark ===\n");
    
    /* Memory bandwidth test */
    if (shell_argc < 2 || strcmp(shell_argv[1], "mem") == 0) {
        printk("Memory bandwidth test...\n");
        
        size_t test_size = 1024 * 1024;  /* 1MB */
        char* buf1 = kmalloc(test_size);
        char* buf2 = kmalloc(test_size);
        
        if (buf1 && buf2) {
            uint64_t start = timer_perf_start();
            
            /* memcpy test */
            for (int iter = 0; iter < 100; iter++) {
                memcpy(buf2, buf1, test_size);
            }
            
            uint64_t elapsed_ns = timer_perf_end(start);
            uint64_t total_bytes = (uint64_t)test_size * 100;
            uint64_t mb_per_sec = (total_bytes * 1000000000ULL) / (elapsed_ns * 1024 * 1024);
            
            printk("  memcpy: %lu MB/s\n", mb_per_sec);
        }
        
        if (buf1) kfree(buf1);
        if (buf2) kfree(buf2);
    }
    
    printk("========================\n\n");
}

static void cmd_net(void)
{
    printk("\n=== Network Status ===\n");
    printk("Interface    Status    IP Address        MAC Address\n");
    printk("eth0         DOWN      0.0.0.0/0        00:00:00:00:00:00\n");
    printk("(Network stack not yet initialized)\n");
    printk("======================\n\n");
}

static void cmd_crypto(void)
{
    printk("\n=== Cryptographic Tools ===\n");
    printk("Available algorithms:\n");
    printk("  - AES-256-GCM\n");
    printk("  - ChaCha20-Poly1305\n");
    printk("  - Ed25519 (signing)\n");
    printk("  - SHA-256 / SHA-512\n");
    printk("  - Argon2id (key derivation)\n");
    printk("  - X25519 (key exchange)\n");
    printk("  - HKDF-SHA256\n");
    printk("===========================\n\n");
}

static void cmd_pen(void)
{
    printk("\n╔═══════════════════════════════════════════════════════════╗\n");
    printk("║         CRIMSON PENTESTING ARSENAL v0.1                  ║\n");
    printk("╠═══════════════════════════════════════════════════════════╣\n");
    printk("║  Network Tools:                                          ║\n");
    printk("║    nmap        - Network scanner (port, host discovery)  ║\n");
    printk("║    tcpdump     - Packet capture and analysis             ║\n");
    printk("║    wireshark   - GUI protocol analyzer                   ║\n");
    printk("║    netcat      - Network swiss army knife                ║\n");
    printk("║    aircrack-ng - WiFi security auditing                  ║\n");
    printk("║                                                          ║\n");
    printk("║  Wireless Tools:                                         ║\n");
    printk("║    kismet      - Wireless network detector/sniffer       ║\n");
    printk("║    reaver      - WPS PIN brute force                     ║\n");
    printk("║    wifite      - Automated WiFi auditor                  ║\n");
    printk("║                                                          ║\n");
    printk("║  Bluetooth Tools:                                        ║\n");
    printk("║    bluez       - Bluetooth stack and tools               ║\n");
    printk("║    blueranger  - Bluetooth range estimator               ║\n");
    printk("║                                                          ║\n");
    printk("║  RFID/NFC Tools:                                         ║\n");
    printk("║    proxmark3   - RFID analyzer                           ║\n");
    printk("║    mfoc        - Mifare Classic Offline Cracker          ║\n");
    printk("║                                                          ║\n");
    printk("║  SDR Tools:                                              ║\n");
    printk("║    rtl-sdr     - RTL2832U SDR receiver                   ║\n");
    printk("║    hackrf      - HackRF support                          ║\n");
    printk("║                                                          ║\n");
    printk("║  Exploitation:                                           ║\n");
    printk("║    metasploit  - Exploitation framework                  ║\n");
    printk("║    sqlmap      - SQL injection tool                      ║\n");
    printk("║    nikto       - Web server scanner                      ║\n");
    printk("║                                                          ║\n");
    printk("║  Forensics:                                              ║\n");
    printk("║    sleuthkit   - Filesystem forensics                    ║\n");
    printk("║    autopsy     - Forensic browser                        ║\n");
    printk("║    foremost    - File recovery                           ║\n");
    printk("║                                                          ║\n");
    printk("║  Crypto/Analysis:                                        ║\n");
    printk("║    john        - Password cracker                        ║\n");
    printk("║    hashcat     - GPU-accelerated password recovery       ║\n");
    printk("║    openssl     - Cryptographic toolkit                   ║\n");
    printk("║                                                          ║\n");
    printk("║  Hardware Hacking:                                       ║\n");
    printk("║    flashrom    - Flash chip programmer                   ║\n");
    printk("║    openocd     - JTAG/SWD debugger                       ║\n");
    printk("║    buspirate   - Bus Pirate interface                    ║\n");
    printk("╚═══════════════════════════════════════════════════════════╝\n");
    printk("\nNote: This is a framework listing. Individual tools\n");
    printk("will be available via 'pkg install <tool>' when the\n");
    printk("Crimson Package Manager is fully operational.\n\n");
}

static void cmd_pkg(void)
{
    /* Parse arguments from shell buffer */
    char* argv[SHELL_MAX_ARGS];
    int argc = 0;
    char* p = shell_buffer;

    /* Skip command name */
    while (*p && *p != ' ') p++;
    while (*p == ' ') p++;

    /* Build argument list */
    while (*p && argc < SHELL_MAX_ARGS) {
        argv[argc++] = p;
        while (*p && *p != ' ') p++;
        if (*p) { *p = '\0'; p++; }
        while (*p == ' ') p++;
    }

    if (argc == 0) {
        pkg_shell_help();
        return;
    }

    pkg_shell_dispatch(argv[0], argc - 1, &argv[1]);
}

static void cmd_sec(void)
{
    /* Parse arguments from shell buffer */
    char* argv[SHELL_MAX_ARGS];
    int argc = 0;
    char* p = shell_buffer;

    /* Skip command name */
    while (*p && *p != ' ') p++;
    while (*p == ' ') p++;

    /* Build argument list */
    while (*p && argc < SHELL_MAX_ARGS) {
        argv[argc++] = p;
        while (*p && *p != ' ') p++;
        if (*p) { *p = '\0'; p++; }
        while (*p == ' ') p++;
    }

    if (argc == 0) {
        security_shell_help();
        return;
    }

    security_shell_dispatch(argv[0], argc - 1, &argv[1]);
}
