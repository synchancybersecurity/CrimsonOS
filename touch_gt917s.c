/*
 * Crimson OS - Goodix GT917S Touch Controller Driver
 * Board: PinePhone Pro
 * I2C address: 0x14 (INT low at boot) or 0x5D (INT high at boot)
 * Bus: Allwinner A64 TWI1 at 0x01C2AC00 (400 kHz fast-mode)
 *
 * GT917S register map (compatible with GT911):
 *   0x8140  Product ID (4 bytes ASCII)
 *   0x814E  Status register:  bit7=buf_rdy, bits[3:0]=finger_count
 *   0x8150  Point 0 data (8 bytes per point × up to 10 points)
 *           Byte 0:   track_id (bits[3:0])
 *           Byte 1-2: x coordinate (LE uint16)
 *           Byte 3-4: y coordinate (LE uint16)
 *           Byte 5-6: touch area (LE uint16)
 *           Byte 7:   reserved
 *   Writing 0 to 0x814E acknowledges the interrupt.
 *
 * Reference: Goodix GT911 Programming Guide v1.4 §4; A64 User Manual §7.4 TWI
 */

#include <crimson/types.h>
#include <crimson/printk.h>
#include <crimson/timer.h>

/* ---- Touch point type (must match touch_pipeline.c) ---- */
typedef struct {
    int32_t  x, y;
    uint32_t pressure;
    uint32_t area;
    uint8_t  id;
    uint8_t  active;
} touch_point_t;

/* ---- A64 TWI1 registers ---- */
#define TWI_BASE        0x01C2AC00UL
#define TWI_ADDR        (TWI_BASE + 0x00)   /* Slave address (7-bit in [7:1]) */
#define TWI_XADDR       (TWI_BASE + 0x04)   /* Extended slave address */
#define TWI_DATA        (TWI_BASE + 0x08)   /* Data register */
#define TWI_CNTR        (TWI_BASE + 0x0C)   /* Control register */
#define TWI_STAT        (TWI_BASE + 0x10)   /* Status register */
#define TWI_CCR         (TWI_BASE + 0x14)   /* Clock control register */
#define TWI_SRST        (TWI_BASE + 0x18)   /* Software reset */
#define TWI_EFR         (TWI_BASE + 0x1C)   /* Enhanced feature register */
#define TWI_LCR         (TWI_BASE + 0x20)   /* Line control register */

/* TWI_CNTR bits */
#define TWI_CNTR_INT_EN     (1U << 7)
#define TWI_CNTR_BUS_EN     (1U << 6)
#define TWI_CNTR_M_STA      (1U << 5)   /* Generate START */
#define TWI_CNTR_M_STP      (1U << 4)   /* Generate STOP */
#define TWI_CNTR_INT_FLAG   (1U << 3)   /* Interrupt flag; write 0 to clear */
#define TWI_CNTR_ACK        (1U << 2)   /* Assert ACK on next received byte */

/* TWI status codes */
#define TWI_STAT_BUS_ERROR  0x00
#define TWI_STAT_START      0x08
#define TWI_STAT_RSTART     0x10
#define TWI_STAT_SLAW_ACK   0x18   /* SLA+W sent, ACK */
#define TWI_STAT_SLAW_NACK  0x20
#define TWI_STAT_DATA_ACK   0x28   /* Data sent, ACK */
#define TWI_STAT_DATA_NACK  0x30
#define TWI_STAT_ARB_LOST   0x38
#define TWI_STAT_SLAR_ACK   0x40   /* SLA+R sent, ACK */
#define TWI_STAT_SLAR_NACK  0x48
#define TWI_STAT_RXDATA_ACK 0x50   /* Data received, ACK sent */
#define TWI_STAT_RXDATA_NAK 0x58   /* Data received, NACK sent (last byte) */
#define TWI_STAT_IDLE       0xF8

/* GT917S constants */
#define GT_ADDR_PRIMARY     0x14
#define GT_ADDR_SECONDARY   0x5D
#define GT_REG_STATUS       0x814E
#define GT_REG_POINT_BASE   0x8150
#define GT_STATUS_BUF_RDY   (1U << 7)
#define GT_MAX_CONTACTS     10
#define GT_POINT_STRIDE     8

/* ---- MMIO helpers ---- */
static inline uint32_t twi_rd(uintptr_t r) { return *(volatile uint32_t*)r; }
static inline void     twi_wr(uintptr_t r, uint32_t v) { *(volatile uint32_t*)r = v; }

/* ---- Driver state ---- */
static uint8_t g_gt_addr = GT_ADDR_PRIMARY;
static int     g_gt_ready = 0;

/* ---- TWI polling helpers ---- */

static int twi_wait_flag(uint32_t timeout_us)
{
    while (timeout_us--) {
        if (twi_rd(TWI_CNTR) & TWI_CNTR_INT_FLAG) return 0;
        /* ~1 µs spin */
        for (volatile int i = 0; i < 72; i++);
    }
    return -1;   /* timeout */
}

static int twi_check_stat(uint8_t expected)
{
    return (twi_rd(TWI_STAT) == expected) ? 0 : -1;
}

/* Clear the interrupt flag to advance the state machine */
static void twi_clr_flag(void)
{
    uint32_t v = twi_rd(TWI_CNTR);
    v &= ~TWI_CNTR_INT_FLAG;
    twi_wr(TWI_CNTR, v);
}

/* ---- TWI transaction ---- */

/* Write `wlen` bytes from `wbuf` then read `rlen` bytes into `rbuf`.
 * Uses repeated-START for register-address-then-read sequences. */
static int twi_write_read(uint8_t addr7,
                           const uint8_t* wbuf, uint8_t wlen,
                           uint8_t* rbuf, uint8_t rlen)
{
    int err = 0;

    /* --- START --- */
    twi_wr(TWI_CNTR, TWI_CNTR_BUS_EN | TWI_CNTR_M_STA);
    if (twi_wait_flag(10000) < 0) { err = -1; goto stop; }
    if (twi_check_stat(TWI_STAT_START) < 0) { err = -2; goto stop; }
    twi_clr_flag();

    /* --- SLA+W --- */
    twi_wr(TWI_DATA, (uint32_t)(addr7 << 1) | 0);  /* write */
    if (twi_wait_flag(10000) < 0) { err = -3; goto stop; }
    if (twi_check_stat(TWI_STAT_SLAW_ACK) < 0) { err = -4; goto stop; }
    twi_clr_flag();

    /* --- Write bytes --- */
    for (uint8_t i = 0; i < wlen; i++) {
        twi_wr(TWI_DATA, wbuf[i]);
        if (twi_wait_flag(10000) < 0) { err = -5; goto stop; }
        if (twi_check_stat(TWI_STAT_DATA_ACK) < 0) { err = -6; goto stop; }
        twi_clr_flag();
    }

    if (rlen == 0) goto stop;

    /* --- Repeated START --- */
    twi_wr(TWI_CNTR, TWI_CNTR_BUS_EN | TWI_CNTR_M_STA);
    if (twi_wait_flag(10000) < 0) { err = -7; goto stop; }
    if (twi_check_stat(TWI_STAT_RSTART) < 0) { err = -8; goto stop; }
    twi_clr_flag();

    /* --- SLA+R --- */
    twi_wr(TWI_DATA, (uint32_t)(addr7 << 1) | 1);  /* read */
    if (twi_wait_flag(10000) < 0) { err = -9; goto stop; }
    if (twi_check_stat(TWI_STAT_SLAR_ACK) < 0) { err = -10; goto stop; }
    twi_clr_flag();

    /* --- Read bytes (ACK all except last) --- */
    for (uint8_t i = 0; i < rlen; i++) {
        uint32_t ctrl = TWI_CNTR_BUS_EN;
        if (i < rlen - 1) ctrl |= TWI_CNTR_ACK;   /* ACK all but last */
        twi_wr(TWI_CNTR, ctrl);
        if (twi_wait_flag(10000) < 0) { err = -11; goto stop; }
        uint8_t expected = (i < rlen - 1) ? TWI_STAT_RXDATA_ACK : TWI_STAT_RXDATA_NAK;
        if (twi_check_stat(expected) < 0) { err = -12; goto stop; }
        rbuf[i] = (uint8_t)twi_rd(TWI_DATA);
        twi_clr_flag();
    }

stop:
    /* --- STOP --- */
    twi_wr(TWI_CNTR, TWI_CNTR_BUS_EN | TWI_CNTR_M_STP);
    /* Wait until bus returns to idle */
    uint32_t to = 5000;
    while (to-- && (twi_rd(TWI_STAT) != TWI_STAT_IDLE))
        ;
    return err;
}

/* Read `len` bytes from GT917S 16-bit register address `reg` */
static int gt_read_reg(uint16_t reg, uint8_t* buf, uint8_t len)
{
    uint8_t addr[2];
    addr[0] = (uint8_t)(reg >> 8);
    addr[1] = (uint8_t)(reg & 0xFF);
    return twi_write_read(g_gt_addr, addr, 2, buf, len);
}

/* Write single byte to GT917S register */
static int gt_write_reg(uint16_t reg, uint8_t val)
{
    uint8_t buf[3];
    buf[0] = (uint8_t)(reg >> 8);
    buf[1] = (uint8_t)(reg & 0xFF);
    buf[2] = val;
    uint8_t dummy;
    return twi_write_read(g_gt_addr, buf, 3, &dummy, 0);
}

/* ---- TWI1 clock init ---- */
/*
 * A64 CCU: TWI1 clock is sourced from APB1 (typically 24 MHz).
 * TWI_CCR[2:0] = CLK_M (denominator), TWI_CCR[5:3] = CLK_N (shift).
 * f_SCL = f_APB1 / (10 × 2^CLK_N × (CLK_M + 1))
 * For 400 kHz fast-mode with f_APB1 = 24 MHz:
 *   24000000 / (10 × 2^1 × (3+1)) = 24000000 / 80 = 300 kHz (close enough)
 *   Or use CLK_M=2, CLK_N=0: 24e6/(10×1×3) = 800 kHz — too fast
 *   CLK_M=1, CLK_N=1: 24e6/(10×2×2) = 600 kHz
 *   CLK_M=1, CLK_N=2: 24e6/(10×4×2) = 300 kHz  ← use this
 */
static void twi_clock_init(void)
{
    /* APB1 bus clock enable for TWI1 in A64 CCU is bit 21 of BUS_CLK_GATE1 */
    #define A64_CCU_BUS_CLK_GATE2  0x01C20068UL
    #define A64_CCU_BUS_SOFT_RST4  0x01C202D0UL
    *(volatile uint32_t*)A64_CCU_BUS_CLK_GATE2 |= (1U << 21);  /* TWI1 */
    /* De-assert reset */
    *(volatile uint32_t*)A64_CCU_BUS_SOFT_RST4 |= (1U << 21);
    timer_delay_ms(1);

    /* CLK_M=1 (bits[2:0]=1), CLK_N=2 (bits[5:3]=2) → ~300 kHz */
    twi_wr(TWI_CCR, (2U << 3) | 1U);
    /* Software reset */
    twi_wr(TWI_SRST, 1);
    /* Enable bus */
    twi_wr(TWI_CNTR, TWI_CNTR_BUS_EN);
}

/* ---- Public: init and read ---- */

int touch_gt917s_init(void)
{
    twi_clock_init();

    /* Probe primary address first */
    uint8_t pid[4] = {0};
    if (gt_read_reg(0x8140, pid, 4) < 0) {
        /* Try secondary address */
        g_gt_addr = GT_ADDR_SECONDARY;
        if (gt_read_reg(0x8140, pid, 4) < 0) {
            printk(KERN_WARN "touch: GT917S not found on I2C\n");
            return -1;
        }
    }

    printk(KERN_INFO "touch: GT917S found at 0x%02x, product id: %.4s\n",
           g_gt_addr, (char*)pid);

    /* Clear status register to confirm comms */
    gt_write_reg(GT_REG_STATUS, 0x00);

    g_gt_ready = 1;
    return 0;
}

/*
 * touch_read_hw - Read raw contacts from GT917S.
 * Called from touch_irq_handler() in touch_pipeline.c.
 * Returns the number of active touch points (0-10).
 */
int touch_read_hw(touch_point_t* pts, uint32_t max)
{
    if (!g_gt_ready) return 0;

    /* Read status register */
    uint8_t status;
    if (gt_read_reg(GT_REG_STATUS, &status, 1) < 0) return 0;

    if (!(status & GT_STATUS_BUF_RDY)) return 0;  /* no new data */

    uint32_t count = status & 0x0F;
    if (count > GT_MAX_CONTACTS) count = GT_MAX_CONTACTS;
    if (count > max) count = max;

    if (count > 0) {
        /* Read all contact bytes in one transaction: count × 8 bytes */
        uint8_t raw[GT_MAX_CONTACTS * GT_POINT_STRIDE];
        uint8_t read_len = (uint8_t)(count * GT_POINT_STRIDE);
        if (gt_read_reg(GT_REG_POINT_BASE, raw, read_len) < 0) {
            gt_write_reg(GT_REG_STATUS, 0);
            return 0;
        }

        for (uint32_t i = 0; i < count; i++) {
            const uint8_t* p = raw + i * GT_POINT_STRIDE;
            pts[i].id       = p[0] & 0x0F;
            pts[i].x        = (int32_t)((uint16_t)p[1] | ((uint16_t)p[2] << 8));
            pts[i].y        = (int32_t)((uint16_t)p[3] | ((uint16_t)p[4] << 8));
            pts[i].area     = (uint32_t)((uint16_t)p[5] | ((uint16_t)p[6] << 8));
            pts[i].pressure = pts[i].area;  /* GT917S uses area as pressure proxy */
            pts[i].active   = 1;
        }
    }

    /* Acknowledge: clear buffer-ready bit */
    gt_write_reg(GT_REG_STATUS, 0x00);

    return (int)count;
}
