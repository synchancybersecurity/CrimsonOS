/* Copyright (c) 2026 SynChanCyberSecurity LLC. All Rights Reserved. */
/*
 * Crimson OS — Quectel EG25-G LTE Modem Driver
 * Board: PinePhone Pro (Allwinner A64)
 *
 * The EG25-G is connected to the A64 via:
 *   UART3  (0x01C28C00) — AT commands at 115200 baud (PD0=TX, PD1=RX)
 *   PWRKEY — PB2 (GPIO34): hold HIGH ≥ 500 ms to power on/off
 *   RESET_N — PC4 (GPIO68): assert LOW ≥ 150 ms to reset
 *   STATUS  — PC20 or PL2: HIGH = modem on (polled, not interrupt-driven)
 *
 * A64 GPIO base:  0x01C20800
 * A64 UART3 base: 0x01C28C00 (16550-compatible)
 *
 * This file implements the cell_driver_t interface declared in cellular.h.
 * Register this driver with cellular_probe(&eg25g_driver) at boot.
 *
 * Reference: Quectel EG25-G AT Commands Manual v2.0
 *            Allwinner A64 User Manual §7.4 (UART), §4.4 (GPIO/PIO)
 */

#include <crimson/types.h>
#include <crimson/printk.h>
#include <crimson/memory.h>
#include <crimson/spinlock.h>
#include <crimson/string.h>
#include <crimson/timer.h>
#include <crimson/gpio.h>
#include <crimson/net.h>
#include <crimson/cellular.h>

/* ── A64 UART3 registers ─────────────────────────────────────── */

#define UART3_BASE      0x01C28C00UL
#define UART_RBR        (UART3_BASE + 0x00)   /* Receive buffer (read) */
#define UART_THR        (UART3_BASE + 0x00)   /* Transmit holding (write) */
#define UART_DLL        (UART3_BASE + 0x00)   /* Divisor latch low (DLAB=1) */
#define UART_DLH        (UART3_BASE + 0x04)   /* Divisor latch high (DLAB=1) */
#define UART_IER        (UART3_BASE + 0x04)   /* Interrupt enable */
#define UART_IIR        (UART3_BASE + 0x08)   /* Interrupt ID (read) */
#define UART_FCR        (UART3_BASE + 0x08)   /* FIFO control (write) */
#define UART_LCR        (UART3_BASE + 0x0C)   /* Line control */
#define UART_MCR        (UART3_BASE + 0x10)   /* Modem control */
#define UART_LSR        (UART3_BASE + 0x14)   /* Line status */
#define UART_MSR        (UART3_BASE + 0x18)   /* Modem status */

#define LSR_DR          (1U << 0)   /* Data ready */
#define LSR_THRE        (1U << 5)   /* TX holding register empty */
#define LCR_DLAB        (1U << 7)   /* Divisor latch access bit */
#define LCR_8N1         0x03        /* 8 bits, no parity, 1 stop bit */

/* ── A64 GPIO (PIO) ─────────────────────────────────────────── */

#define PIO_BASE        0x01C20800UL

/* Port B (PB): offset 0x024 */
#define PB_CFG0         (PIO_BASE + 0x024)   /* PB0-PB7 config */
#define PB_DAT          (PIO_BASE + 0x034)   /* PB data register */

/* Port C (PC): offset 0x048 */
#define PC_CFG0         (PIO_BASE + 0x048)   /* PC0-PC7 config */
#define PC_DAT          (PIO_BASE + 0x058)   /* PC data register */

/* PWRKEY = PB2: CFG0 bits [11:8], function=1 (OUTPUT) */
#define PWRKEY_PIN_MASK     (0xFU << 8)
#define PWRKEY_PIN_OUT      (0x1U << 8)
#define PWRKEY_BIT          (1U << 2)

/* RESET_N = PC4: CFG0 bits [19:16], function=1 (OUTPUT) */
#define RESET_PIN_MASK      (0xFU << 16)
#define RESET_PIN_OUT       (0x1U << 16)
#define RESET_BIT           (1U << 4)

static inline uint32_t pio_rd(uintptr_t r) { return *(volatile uint32_t*)r; }
static inline void     pio_wr(uintptr_t r, uint32_t v) { *(volatile uint32_t*)r = v; }

static inline uint32_t uart_rd(uintptr_t r) { return *(volatile uint32_t*)r; }
static inline void     uart_wr(uintptr_t r, uint32_t v) { *(volatile uint32_t*)r = v; }

/* ── A64 UART3 init ─────────────────────────────────────────── */

static void uart3_init(void)
{
    /* A64 CCU: enable UART3 clock and de-assert reset */
    #define A64_CCU_BASE        0x01C20000UL
    #define CCU_BUS_CLK_GATE1   (A64_CCU_BASE + 0x064)   /* bit 19 = UART3 */
    #define CCU_BUS_SOFT_RST1   (A64_CCU_BASE + 0x2C4)   /* bit 19 = UART3 */

    *(volatile uint32_t*)CCU_BUS_CLK_GATE1 |= (1U << 19);
    *(volatile uint32_t*)CCU_BUS_SOFT_RST1 |= (1U << 19);
    timer_delay_ms(1);

    /* Configure PD0=UART3_TX, PD1=UART3_RX (mux function 3) */
    #define PD_CFG0             (PIO_BASE + 0x06C)
    uint32_t pd_cfg = pio_rd(PD_CFG0);
    pd_cfg &= ~0x000000FF;      /* clear PD0 and PD1 config */
    pd_cfg |=  0x00000033;      /* PD0=func3 (TX), PD1=func3 (RX) */
    pio_wr(PD_CFG0, pd_cfg);

    /* Baud rate 115200 with UART source 24 MHz APB1:
     * Divisor = 24000000 / (16 × 115200) = 13 */
    uart_wr(UART_LCR,  LCR_DLAB);
    uart_wr(UART_DLL,  13);
    uart_wr(UART_DLH,  0);
    uart_wr(UART_LCR,  LCR_8N1);
    uart_wr(UART_FCR,  0x07);    /* enable + clear TX/RX FIFOs */
    uart_wr(UART_IER,  0x00);    /* no interrupts (polling) */
    uart_wr(UART_MCR,  0x03);    /* DTR + RTS asserted */
}

static void uart3_putc(char c)
{
    while (!(uart_rd(UART_LSR) & LSR_THRE)) ;
    uart_wr(UART_THR, (uint32_t)(uint8_t)c);
}

static int uart3_getc_timeout(uint32_t timeout_ms)
{
    uint64_t deadline = timer_get_uptime_ms() + timeout_ms;
    while (timer_get_uptime_ms() < deadline) {
        if (uart_rd(UART_LSR) & LSR_DR)
            return (int)(uart_rd(UART_RBR) & 0xFF);
    }
    return -1;
}

static void uart3_puts(const char* s)
{
    while (*s) uart3_putc(*s++);
}

/* ── GPIO power control ──────────────────────────────────────── */

static void gpio_modem_init(void)
{
    /* Set PB2 (PWRKEY) as output */
    uint32_t pb_cfg = pio_rd(PB_CFG0);
    pb_cfg = (pb_cfg & ~PWRKEY_PIN_MASK) | PWRKEY_PIN_OUT;
    pio_wr(PB_CFG0, pb_cfg);

    /* Set PC4 (RESET_N) as output */
    uint32_t pc_cfg = pio_rd(PC_CFG0);
    pc_cfg = (pc_cfg & ~RESET_PIN_MASK) | RESET_PIN_OUT;
    pio_wr(PC_CFG0, pc_cfg);

    /* De-assert both (idle state) */
    pio_wr(PB_DAT, pio_rd(PB_DAT) & ~PWRKEY_BIT);
    pio_wr(PC_DAT, pio_rd(PC_DAT) | RESET_BIT);   /* RESET_N high = not reset */
}

static void modem_pwrkey_pulse(uint32_t ms)
{
    pio_wr(PB_DAT, pio_rd(PB_DAT) | PWRKEY_BIT);
    timer_delay_ms(ms);
    pio_wr(PB_DAT, pio_rd(PB_DAT) & ~PWRKEY_BIT);
}

static void modem_reset_pulse(void)
{
    pio_wr(PC_DAT, pio_rd(PC_DAT) & ~RESET_BIT);  /* assert RESET_N low */
    timer_delay_ms(200);
    pio_wr(PC_DAT, pio_rd(PC_DAT) | RESET_BIT);   /* release */
    timer_delay_ms(5000);   /* EG25-G needs ~5 s to boot after reset */
}

/* ── AT command engine ──────────────────────────────────────── */

/* cell_modem_t and cell_driver_t defined in cellular.h */

/*
 * at_cmd - Send `cmd` and collect response into `resp` up to `resp_size`.
 * Returns 0 on OK, -1 on timeout/error, 1 on CME ERROR.
 */
static int at_cmd(const char* cmd, char* resp, uint32_t resp_size, uint32_t timeout_ms)
{
    /* Flush RX FIFO */
    while (uart_rd(UART_LSR) & LSR_DR) (void)uart_rd(UART_RBR);

    uart3_puts(cmd);
    uart3_puts("\r\n");

    uint32_t pos = 0;
    uint64_t deadline = timer_get_uptime_ms() + timeout_ms;

    while (timer_get_uptime_ms() < deadline) {
        int c = uart3_getc_timeout(5);
        if (c < 0) continue;
        if (pos < resp_size - 1) resp[pos++] = (char)c;
        resp[pos] = '\0';

        /* Check for terminal strings */
        if (pos >= 4 && memcmp(resp + pos - 4, "\r\nOK", 4) == 0) return 0;
        if (pos >= 7 && memcmp(resp + pos - 7, "\r\nERROR", 7) == 0) return -1;
        if (pos >= 10 && memcmp(resp + pos - 8, "+CME ERROR", 10) == 0) return 1;
        if (pos >= 10 && memcmp(resp + pos - 8, "+CMS ERROR", 10) == 0) return 1;
    }
    return -2;  /* timeout */
}

/* Extract value after `prefix` in response string */
static const char* at_extract(const char* resp, const char* prefix)
{
    const char* p = strstr(resp, prefix);
    return p ? (p + strlen(prefix)) : NULL;
}

/* ── cell_driver_t hook implementations ─────────────────────── */

static int eg25g_probe(cell_modem_t* modem)
{
    (void)modem;
    gpio_modem_init();
    uart3_init();

    /* Try to contact modem; may already be on */
    char resp[256];
    if (at_cmd("AT", resp, sizeof(resp), 1000) < 0) {
        /* Not responding — try power-on */
        printk(KERN_INFO "eg25g: powering on modem...\n");
        modem_pwrkey_pulse(600);   /* 500-1000 ms for power on */
        timer_delay_ms(8000);      /* boot time */
        if (at_cmd("AT", resp, sizeof(resp), 2000) < 0) {
            printk(KERN_WARN "eg25g: modem not responding after power on\n");
            return -1;
        }
    }

    /* Disable echo */
    at_cmd("ATE0",         resp, sizeof(resp), 500);
    /* Verbose error codes */
    at_cmd("AT+CMEE=2",    resp, sizeof(resp), 500);
    /* Get IMEI */
    at_cmd("AT+CGSN",      resp, sizeof(resp), 1000);
    /* Get IMSI */
    at_cmd("AT+CIMI",      resp, sizeof(resp), 1000);
    /* Get ICCID */
    at_cmd("AT+QCCID",     resp, sizeof(resp), 1000);
    /* Get firmware version */
    at_cmd("ATI",          resp, sizeof(resp), 1000);
    printk(KERN_INFO "eg25g: modem info: %s\n", resp);

    return 0;
}

static void eg25g_remove(cell_modem_t* modem)
{
    (void)modem;
    char resp[64];
    at_cmd("AT+CFUN=0", resp, sizeof(resp), 5000);  /* minimal function */
}

static int eg25g_at_command(cell_modem_t* modem,
                              const char* cmd, char* resp,
                              uint32_t resp_size, uint32_t timeout_ms)
{
    (void)modem;
    return at_cmd(cmd, resp, resp_size, timeout_ms);
}

static int eg25g_get_signal(cell_modem_t* modem)
{
    char resp[128];
    if (at_cmd("AT+CSQ", resp, sizeof(resp), 2000) < 0) return -1;
    const char* p = at_extract(resp, "+CSQ: ");
    if (!p) return -1;
    int rssi_raw = atoi(p);
    /* Convert: -113 + 2*rssi dBm; 99=unknown */
    if (rssi_raw == 99) return -1;
    modem->rssi = -113 + 2 * rssi_raw;

    /* Extended signal quality */
    at_cmd("AT+QCSQ", resp, sizeof(resp), 2000);
    p = at_extract(resp, "+QCSQ: \"LTE\",");
    if (p) {
        modem->rat = 2; /* RAT_LTE */
        /* Parse RSSI,RSRP,SINR,RSRQ */
        /* Parse: skip first comma-field, then rsrp,sinr,rsrq */
        while (*p && *p != ',') { p++; } if (*p) { p++; }
        modem->lte.rsrp = (int32_t)atoi(p);
        while (*p && *p != ',') { p++; } if (*p) { p++; }
        modem->lte.snr  = (int32_t)atoi(p);
        while (*p && *p != ',') { p++; } if (*p) { p++; }
        modem->lte.rsrq = (int32_t)atoi(p);
    }
    return 0;
}

static int eg25g_get_registration(cell_modem_t* modem)
{
    char resp[256];
    at_cmd("AT+CREG?",  resp, sizeof(resp), 2000);
    const char* p = at_extract(resp, "+CREG: ");
    if (p) {
        (void)atoi(p);  /* skip n */
        const char* comma = strchr(p, ',');
        if (comma) modem->reg_state = (uint32_t)atoi(comma + 1);
    }
    at_cmd("AT+CEREG?", resp, sizeof(resp), 2000);

    /* Get operator name */
    at_cmd("AT+COPS?", resp, sizeof(resp), 5000);
    p = at_extract(resp, "+COPS: ");
    if (p) {
        /* Format: mode,format,"name",AcT */
        const char* start = strchr(p, '"');
        if (start) {
            start++;
            const char* end = strchr(start, '"');
            if (end) {
                uint32_t len = (uint32_t)(end - start);
                if (len > 31) len = 31;
                strncpy(modem->operator, start, len);
                modem->operator[len] = '\0';
            }
        }
    }
    return 0;
}

static int eg25g_enter_pin(cell_modem_t* modem, const char* pin)
{
    (void)modem;
    char cmd[32], resp[128];
    snprintf(cmd, sizeof(cmd), "AT+CPIN=%s", pin);
    return at_cmd(cmd, resp, sizeof(resp), 5000);
}

static int eg25g_start_call(cell_modem_t* modem, const char* number)
{
    (void)modem;
    char cmd[48], resp[128];
    snprintf(cmd, sizeof(cmd), "ATD%s;", number);  /* voice call */
    return at_cmd(cmd, resp, sizeof(resp), 10000);
}

static int eg25g_answer_call(cell_modem_t* modem)
{
    (void)modem;
    char resp[64];
    return at_cmd("ATA", resp, sizeof(resp), 3000);
}

static int eg25g_hangup_call(cell_modem_t* modem)
{
    (void)modem;
    char resp[64];
    return at_cmd("ATH", resp, sizeof(resp), 3000);
}

static int eg25g_send_dtmf(cell_modem_t* modem, char tone)
{
    (void)modem;
    char cmd[32], resp[64];
    snprintf(cmd, sizeof(cmd), "AT+VTS=%c", tone);
    return at_cmd(cmd, resp, sizeof(resp), 2000);
}

static int eg25g_send_ussd(cell_modem_t* modem, const char* code)
{
    (void)modem;
    char cmd[128], resp[256];
    snprintf(cmd, sizeof(cmd), "AT+CUSD=1,\"%s\",15", code);
    return at_cmd(cmd, resp, sizeof(resp), 10000);
}

/* GSM 7-bit alphabet packing (TP-UD in SMS PDU) */
static uint32_t gsm7_pack(const char* text, uint8_t* out)
{
    uint32_t len = strlen(text);
    uint32_t out_len = (len * 7 + 7) / 8;
    memset(out, 0, out_len);
    for (uint32_t i = 0; i < len; i++) {
        uint8_t c = (uint8_t)(text[i] & 0x7F);
        uint32_t bit_pos = i * 7;
        uint32_t byte_off = bit_pos / 8;
        uint32_t bit_off  = bit_pos % 8;
        out[byte_off] |= c << bit_off;
        if (bit_off > 1 && byte_off + 1 < out_len)
            out[byte_off + 1] |= c >> (8 - bit_off);
    }
    return len;
}

static int eg25g_send_sms_pdu(cell_modem_t* modem,
                                const uint8_t* pdu, uint32_t pdu_len)
{
    (void)modem;
    char cmd[16], resp[64];
    snprintf(cmd, sizeof(cmd), "AT+CMGS=%d", (int)pdu_len - 1);
    /* Send length, wait for '>' prompt */
    uart3_puts(cmd); uart3_puts("\r\n");
    /* Wait for > */
    uint64_t dl = timer_get_uptime_ms() + 3000;
    while (timer_get_uptime_ms() < dl) {
        int c = uart3_getc_timeout(10);
        if (c == '>') break;
    }
    /* Send PDU hex string */
    static const char hex[] = "0123456789ABCDEF";
    for (uint32_t i = 0; i < pdu_len; i++) {
        uart3_putc(hex[pdu[i] >> 4]);
        uart3_putc(hex[pdu[i] & 0xF]);
    }
    uart3_putc(0x1A);  /* Ctrl-Z to send */
    return at_cmd("", resp, sizeof(resp), 15000);
}

static int eg25g_read_sms_pdu(cell_modem_t* modem, uint32_t index,
                                uint8_t* pdu, uint32_t* pdu_len)
{
    (void)modem;
    char cmd[32], resp[512];
    snprintf(cmd, sizeof(cmd), "AT+CMGR=%d", (int)index);
    if (at_cmd(cmd, resp, sizeof(resp), 5000) < 0) return -1;
    const char* p = strstr(resp, "\r\n");
    if (!p) return -1;
    p += 2;
    /* Decode hex pairs */
    uint32_t n = 0;
    while (p[0] && p[1] && n < 256) {
        uint8_t hi = (p[0] >= 'A') ? (p[0] - 'A' + 10) : (p[0] - '0');
        uint8_t lo = (p[1] >= 'A') ? (p[1] - 'A' + 10) : (p[1] - '0');
        pdu[n++] = (uint8_t)((hi << 4) | lo);
        p += 2;
    }
    *pdu_len = n;
    return 0;
}

static int eg25g_delete_sms(cell_modem_t* modem, uint32_t index)
{
    (void)modem;
    char cmd[32], resp[64];
    snprintf(cmd, sizeof(cmd), "AT+CMGD=%d", (int)index);
    return at_cmd(cmd, resp, sizeof(resp), 5000);
}

static int eg25g_setup_data(cell_modem_t* modem, const void* apn_ptr)
{
    (void)modem;
    typedef struct { char apn[64]; char username[32]; char password[32];
                     uint32_t auth_type; uint32_t ip_type; uint32_t active; } apn_t;
    const apn_t* apn = (const apn_t*)apn_ptr;
    char cmd[256], resp[256];
    /* Set PDP context 1 */
    snprintf(cmd, sizeof(cmd), "AT+CGDCONT=1,\"IPV4V6\",\"%s\"", apn->apn);
    if (at_cmd(cmd, resp, sizeof(resp), 5000) < 0) return -1;
    /* Set auth if credentials present */
    if (apn->username[0]) {
        snprintf(cmd, sizeof(cmd), "AT+QICSGP=1,1,\"%s\",\"%s\",\"%s\",%d",
                 apn->apn, apn->username, apn->password, apn->auth_type);
        at_cmd(cmd, resp, sizeof(resp), 5000);
    }
    return 0;
}

/* net_if_t for PPP/direct-IP over EG25-G */
static net_if_t  g_cell_netif;
static uint8_t   g_cell_rx_buf[2048];
static uint32_t  g_cell_rx_pos;

static int cell_netif_transmit(net_if_t* dev, const uint8_t* data, uint32_t len)
{
    (void)dev;
    /* In direct-IP (NDIS) mode, raw IP packets go directly over UART */
    for (uint32_t i = 0; i < len; i++) uart3_putc((char)data[i]);
    return 0;
}

static void cell_netif_poll(net_if_t* dev)
{
    /* Drain any arriving bytes into rx buf; when we have a complete
     * IP packet (detected by IP total_len field), inject into net stack */
    int c;
    while ((c = uart3_getc_timeout(0)) >= 0) {
        if (g_cell_rx_pos < sizeof(g_cell_rx_buf))
            g_cell_rx_buf[g_cell_rx_pos++] = (uint8_t)c;
    }
    if (g_cell_rx_pos >= 20) {
        uint16_t ip_len = (uint16_t)(((uint16_t)g_cell_rx_buf[2] << 8) | g_cell_rx_buf[3]);
        if (g_cell_rx_pos >= ip_len) {
            /* Prepend fake Ethernet header (we set EtherType = 0x0800) */
            uint8_t* frame = kmalloc(ip_len + 14);
            if (frame) {
                memset(frame,     0x00, 6);   /* DA: zero */
                memcpy(frame + 6, g_cell_netif.mac, 6);
                frame[12] = 0x08; frame[13] = 0x00;   /* IPv4 */
                memcpy(frame + 14, g_cell_rx_buf, ip_len);
                net_rx_frame(dev, frame, ip_len + 14);
                kfree(frame);
            }
            /* Shift remaining bytes down */
            uint32_t rem = g_cell_rx_pos - ip_len;
            memmove(g_cell_rx_buf, g_cell_rx_buf + ip_len, rem);
            g_cell_rx_pos = rem;
        }
    }
}

static int eg25g_start_data(cell_modem_t* modem)
{
    (void)modem;
    char resp[256];
    /* Activate PDP context 1 */
    if (at_cmd("AT+CGACT=1,1", resp, sizeof(resp), 30000) < 0) return -1;

    /* Query assigned IP */
    if (at_cmd("AT+CGPADDR=1", resp, sizeof(resp), 5000) == 0) {
        const char* p = at_extract(resp, "+CGPADDR: 1,");
        if (p) {
            /* Parse IPv4: a.b.c.d */
            uint32_t a=0,b=0,c=0,d=0;
            a=(uint32_t)atoi(p); while(*p&&*p!='.') p++; if(*p) p++;
            b=(uint32_t)atoi(p); while(*p&&*p!='.') p++; if(*p) p++;
            c=(uint32_t)atoi(p); while(*p&&*p!='.') p++; if(*p) p++;
            d=(uint32_t)atoi(p);
            g_cell_netif.ip_addr = (a<<24)|(b<<16)|(c<<8)|d;
            printk(KERN_INFO "eg25g: data connection, IP=%u.%u.%u.%u\n",a,b,c,d);
        }
    }

    /* Query DNS */
    if (at_cmd("AT+QIDNSCFG=1", resp, sizeof(resp), 5000) == 0) {
        /* parse primary DNS from response */
    }
    g_cell_netif.dns1 = 0x08080808;  /* fallback 8.8.8.8 */

    /* Switch UART to data mode (direct-IP / NDIS) */
    at_cmd("AT+QNETDEVCTL=1,1,1", resp, sizeof(resp), 5000);

    /* Register net_if if not already done */
    if (g_cell_netif.transmit == NULL) {
        memset(&g_cell_netif, 0, sizeof(g_cell_netif));
        memcpy(g_cell_netif.name, "cell0", 6);
        g_cell_netif.mtu       = 1500;
        g_cell_netif.transmit  = cell_netif_transmit;
        g_cell_netif.poll      = cell_netif_poll;
        /* Fake MAC from IMEI last 6 digits */
        g_cell_netif.mac[0] = 0x02;
        g_cell_netif.mac[1] = 0xCE;
        for (int i = 0; i < 4; i++)
            g_cell_netif.mac[2+i] = (uint8_t)(i * 37 + 1);
        extern void net_register_if(net_if_t*);
        net_register_if(&g_cell_netif);
    }

    modem->data_active = 1;
    return 0;
}

static int eg25g_stop_data(cell_modem_t* modem)
{
    (void)modem;
    char resp[64];
    at_cmd("AT+CGACT=0,1", resp, sizeof(resp), 10000);
    modem->data_active = 0;
    return 0;
}

static int eg25g_gnss_start(cell_modem_t* modem)
{
    (void)modem;
    char resp[128];
    /* Enable GNSS subsystem */
    at_cmd("AT+QGPS=1", resp, sizeof(resp), 5000);
    /* Enable NMEA output for all sentences at 1 Hz */
    at_cmd("AT+QGPSCFG=\"nmeasrc\",1",     resp, sizeof(resp), 2000);
    at_cmd("AT+QGPSCFG=\"gpsnmeatype\",31",resp, sizeof(resp), 2000);
    return 0;
}

static int eg25g_gnss_stop(cell_modem_t* modem)
{
    (void)modem;
    char resp[64];
    return at_cmd("AT+QGPSEND", resp, sizeof(resp), 3000);
}

static int eg25g_gnss_read(cell_modem_t* modem,
                             double* lat, double* lon, double* alt, float* acc)
{
    (void)modem;
    char resp[256];
    if (at_cmd("AT+QGPSLOC=2", resp, sizeof(resp), 5000) < 0) return -1;
    /* Response: +QGPSLOC: <UTC>,<lat>,<lon>,<hdop>,<alt>,<fix>,<cog>,<spkm>,<spkn>,<date>,<nsat> */
    const char* p = at_extract(resp, "+QGPSLOC: ");
    if (!p) return -1;
    /* Skip UTC field, then parse lat/lon as integer degrees × 1e6 */
    while (*p && *p != ',') { p++; } if (*p) { p++; }
    /* lat: integer part + fractional — read as fixed-point */
    long lat_int = atol(p);
    while (*p && *p != '.') p++;
    long lat_frac = 0;
    if (*p == '.') { p++; lat_frac = atol(p); }
    *lat = (double)lat_int + (double)lat_frac * 1e-6;
    while (*p && *p != ',') { p++; } if (*p) { p++; }
    long lon_int = atol(p);
    while (*p && *p != '.') p++;
    long lon_frac = 0;
    if (*p == '.') { p++; lon_frac = atol(p); }
    *lon = (double)lon_int + (double)lon_frac * 1e-6;
    /* skip hdop, read alt */
    while (*p && *p != ',') { p++; } if (*p) { p++; }
    long hdop_i = atol(p);
    *acc = (float)(hdop_i * 5);
    while (*p && *p != ',') { p++; } if (*p) { p++; }
    *alt = (double)atol(p);
    return 0;
}

static int eg25g_get_cell_info(cell_modem_t* modem)
{
    char resp[512];
    at_cmd("AT+QENG=\"servingcell\"", resp, sizeof(resp), 5000);
    /* Parse LTE: +QENG: "servingcell","NOCONN","LTE","FDD",mcc,mnc,cellid,pcid,earfcn,bw,rsrp,rsrq,sinr */
    const char* p = strstr(resp, "\"LTE\"");
    if (p) {
        modem->rat = 2;
        /* Skip to numeric fields: "LTE","FDD",mcc,mnc,... */
        for (int i = 0; i < 4; i++) { while (*p && *p != ',') { p++; } if (*p) { p++; } }
        /* Parse comma-separated: mcc,mnc,cellid(hex),pci,earfcn,bw,rsrp,rsrq,sinr */
        #define NEXT_FIELD(dst, type) do { dst=(type)strtoul(p,NULL,10); while(*p&&*p!=',')p++; if(*p)p++; } while(0)
        NEXT_FIELD(modem->lte.mcc,   uint32_t);
        NEXT_FIELD(modem->lte.mnc,   uint32_t);
        { uint32_t cid=(uint32_t)strtoul(p,NULL,16); (void)cid; while(*p&&*p!=',')p++; if(*p)p++; }
        NEXT_FIELD(modem->lte.pci,   uint32_t);
        NEXT_FIELD(modem->lte.earfcn,uint32_t);
        NEXT_FIELD(modem->lte.bw,    uint32_t);
        NEXT_FIELD(modem->lte.rsrp,  int32_t);
        NEXT_FIELD(modem->lte.rsrq,  int32_t);
        NEXT_FIELD(modem->lte.snr,   int32_t);
        #undef NEXT_FIELD
    }
    return 0;
}

static int eg25g_set_rat(cell_modem_t* modem, uint32_t rat)
{
    (void)modem;
    char cmd[32], resp[64];
    /* scanmode: 0=auto, 1=GSM only, 3=LTE only */
    uint32_t scanmode = (rat == 0) ? 1 : (rat == 2) ? 3 : 0;
    snprintf(cmd, sizeof(cmd), "AT+QCFG=\"nwscanmode\",%u,1", scanmode);
    return at_cmd(cmd, resp, sizeof(resp), 5000);
}

static int eg25g_scan_operators(cell_modem_t* modem)
{
    (void)modem;
    char resp[1024];
    /* Long timeout — operator scan takes up to 3 minutes */
    int ret = at_cmd("AT+COPS=?", resp, sizeof(resp), 180000);
    if (ret == 0)
        printk(KERN_INFO "eg25g: operator scan:\n%s\n", resp);
    return ret;
}

static void eg25g_power_on(cell_modem_t* modem)
{
    (void)modem;
    gpio_modem_init();
    uart3_init();
    printk(KERN_INFO "eg25g: asserting PWRKEY...\n");
    modem_pwrkey_pulse(600);
    timer_delay_ms(8000);   /* wait for RDY URC */
    char resp[128];
    at_cmd("AT", resp, sizeof(resp), 2000);
    printk(KERN_INFO "eg25g: modem on\n");
}

static void eg25g_power_off(cell_modem_t* modem)
{
    (void)modem;
    char resp[64];
    /* Graceful shutdown via AT command first */
    at_cmd("AT+QPOWD=1", resp, sizeof(resp), 5000);
    timer_delay_ms(2000);
    /* Then hold PWRKEY to force off if needed */
    modem_pwrkey_pulse(700);
}

static void eg25g_reset(cell_modem_t* modem)
{
    (void)modem;
    printk(KERN_INFO "eg25g: hardware reset\n");
    modem_reset_pulse();
}

/* ── Driver table and registration ──────────────────────────── */

/* cell_driver_t defined in cellular.h */

static cell_driver_t eg25g_driver = {
    .name            = "Quectel EG25-G",
    .probe           = eg25g_probe,
    .remove          = eg25g_remove,
    .at_command      = eg25g_at_command,
    .send_sms_pdu    = eg25g_send_sms_pdu,
    .read_sms_pdu    = eg25g_read_sms_pdu,
    .delete_sms      = eg25g_delete_sms,
    .setup_data      = eg25g_setup_data,
    .start_data      = eg25g_start_data,
    .stop_data       = eg25g_stop_data,
    .start_call      = eg25g_start_call,
    .answer_call     = eg25g_answer_call,
    .hangup_call     = eg25g_hangup_call,
    .send_dtmf       = eg25g_send_dtmf,
    .send_ussd       = eg25g_send_ussd,
    .get_signal      = eg25g_get_signal,
    .get_registration = eg25g_get_registration,
    .get_cell_info   = eg25g_get_cell_info,
    .enter_pin       = eg25g_enter_pin,
    .set_rat         = eg25g_set_rat,
    .scan_operators  = eg25g_scan_operators,
    .gnss_start      = eg25g_gnss_start,
    .gnss_stop       = eg25g_gnss_stop,
    .gnss_read       = eg25g_gnss_read,
    .power_on        = eg25g_power_on,
    .power_off       = eg25g_power_off,
    .reset           = eg25g_reset,
};

cell_driver_t* modem_eg25g_get_driver(void) { return &eg25g_driver; }

/*
 * modem_eg25g_init - Called from kmain after cellular_init().
 */
int modem_eg25g_init(void)
{
    extern cell_modem_t* cellular_probe(cell_driver_t*);
    cell_modem_t* modem = cellular_probe(&eg25g_driver);
    if (!modem) {
        printk(KERN_WARN "modem: EG25-G not detected\n");
        return -1;
    }
    printk(KERN_INFO "modem: EG25-G ready (IMEI=%s)\n", modem->imei);
    return 0;
}
