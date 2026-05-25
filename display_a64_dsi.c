/* Copyright (c) 2026 SynChanCyberSecurity LLC. All Rights Reserved. */
/*
 * Crimson OS - Allwinner A64 MIPI DSI + D-PHY Controller
 * Board: PinePhone Pro
 * Panel: Xingbangda XBD599 (5.99", 1440×720, MIPI DSI, 4 lanes)
 *
 * Reference: Allwinner A64 User Manual v1.1 (chapters 7.6, 7.7)
 * D-PHY base: 0x01CA0000  (MIPI D-PHY TX)
 * DSI base:   0x01CA1000  (MIPI DSI host)
 * TCON0 base: 0x01C0C000  (Timing Controller)
 * DE2 base:   0x01000000  (Display Engine 2)
 *
 * Lane rate: 4 × 500 Mbps = 2 Gbps aggregate for 1440×720 @ 60 Hz
 */

#include <crimson/types.h>
#include <crimson/printk.h>
#include <crimson/timer.h>

/* ---- Register base addresses ---- */
#define A64_DSI_BASE        0x01CA1000UL
#define A64_DPHY_BASE       0x01CA0000UL
#define A64_TCON0_BASE      0x01C0C000UL
#define A64_DE2_BASE        0x01000000UL
#define A64_CCU_BASE        0x01C20000UL   /* Clock Control Unit */

/* ---- MMIO helpers ---- */
static inline uint32_t reg_read(uintptr_t addr)
{
    return *(volatile uint32_t*)addr;
}
static inline void reg_write(uintptr_t addr, uint32_t val)
{
    *(volatile uint32_t*)addr = val;
}
static inline void reg_set(uintptr_t addr, uint32_t bits)
{
    reg_write(addr, reg_read(addr) | bits);
}
static inline void reg_clr(uintptr_t addr, uint32_t bits)
{
    reg_write(addr, reg_read(addr) & ~bits);
}
static inline void reg_clrset(uintptr_t addr, uint32_t mask, uint32_t val)
{
    reg_write(addr, (reg_read(addr) & ~mask) | (val & mask));
}

/* ---- A64 CCU (Clock Control) ---- */
#define CCU_PLL_MIPI_CTRL   (A64_CCU_BASE + 0x040)
#define CCU_MIPI_CLK        (A64_CCU_BASE + 0x168)
#define CCU_BUS_SOFT_RST0   (A64_CCU_BASE + 0x2C0)
#define CCU_BUS_CLK_GATE1   (A64_CCU_BASE + 0x064)

/* ---- A64 DSI registers (Synopsys DesignWare MIPI DSI Host) ---- */
#define DSI_VERSION         (A64_DSI_BASE + 0x000)
#define DSI_PWR_UP          (A64_DSI_BASE + 0x004)
#define DSI_CLKMGR_CFG      (A64_DSI_BASE + 0x008)
#define DSI_DPI_VCID        (A64_DSI_BASE + 0x00C)
#define DSI_DPI_COLOR_CODING (A64_DSI_BASE + 0x010)
#define DSI_DPI_CFG_POL     (A64_DSI_BASE + 0x014)
#define DSI_DPI_LP_CMD_TIM  (A64_DSI_BASE + 0x018)
#define DSI_PCKHDL_CFG      (A64_DSI_BASE + 0x02C)
#define DSI_GEN_VCID        (A64_DSI_BASE + 0x030)
#define DSI_MODE_CFG        (A64_DSI_BASE + 0x034)
#define DSI_VID_MODE_CFG    (A64_DSI_BASE + 0x038)
#define DSI_VID_PKT_SIZE    (A64_DSI_BASE + 0x03C)
#define DSI_VID_NUM_CHUNKS  (A64_DSI_BASE + 0x040)
#define DSI_VID_NULL_SIZE   (A64_DSI_BASE + 0x044)
#define DSI_VID_HSA_TIME    (A64_DSI_BASE + 0x048)
#define DSI_VID_HBP_TIME    (A64_DSI_BASE + 0x04C)
#define DSI_VID_HLINE_TIME  (A64_DSI_BASE + 0x050)
#define DSI_VID_VSA_LINES   (A64_DSI_BASE + 0x054)
#define DSI_VID_VBP_LINES   (A64_DSI_BASE + 0x058)
#define DSI_VID_VFP_LINES   (A64_DSI_BASE + 0x05C)
#define DSI_VID_VACTIVE_LINES (A64_DSI_BASE + 0x060)
#define DSI_EDPI_CMD_SIZE   (A64_DSI_BASE + 0x064)
#define DSI_CMD_MODE_CFG    (A64_DSI_BASE + 0x068)
#define DSI_GEN_HDR         (A64_DSI_BASE + 0x06C)
#define DSI_GEN_PLD_DATA    (A64_DSI_BASE + 0x070)
#define DSI_CMD_PKT_STATUS  (A64_DSI_BASE + 0x074)
#define DSI_TO_CNT_CFG      (A64_DSI_BASE + 0x078)
#define DSI_HS_RD_TO_CNT    (A64_DSI_BASE + 0x07C)
#define DSI_LP_RD_TO_CNT    (A64_DSI_BASE + 0x080)
#define DSI_HS_WR_TO_CNT    (A64_DSI_BASE + 0x084)
#define DSI_LP_WR_TO_CNT    (A64_DSI_BASE + 0x088)
#define DSI_BTA_TO_CNT      (A64_DSI_BASE + 0x08C)
#define DSI_SDF_3D          (A64_DSI_BASE + 0x090)
#define DSI_LPCLK_CTRL      (A64_DSI_BASE + 0x094)
#define DSI_PHY_TMR_LPCLK   (A64_DSI_BASE + 0x098)
#define DSI_PHY_TMR_CFG     (A64_DSI_BASE + 0x09C)
#define DSI_PHY_RSTZ        (A64_DSI_BASE + 0x0A0)
#define DSI_PHY_IF_CFG      (A64_DSI_BASE + 0x0A4)
#define DSI_PHY_ULPS_CTRL   (A64_DSI_BASE + 0x0A8)
#define DSI_PHY_TX_TRIGGERS (A64_DSI_BASE + 0x0AC)
#define DSI_PHY_STATUS      (A64_DSI_BASE + 0x0B0)
#define DSI_PHY_TST_CTRL0   (A64_DSI_BASE + 0x0B4)
#define DSI_PHY_TST_CTRL1   (A64_DSI_BASE + 0x0B8)
#define DSI_INT_ST0         (A64_DSI_BASE + 0x0BC)
#define DSI_INT_ST1         (A64_DSI_BASE + 0x0C0)
#define DSI_INT_MSK0        (A64_DSI_BASE + 0x0C4)
#define DSI_INT_MSK1        (A64_DSI_BASE + 0x0C8)

/* ---- A64 D-PHY registers ---- */
#define DPHY_TX_CTL         (A64_DPHY_BASE + 0x000)
#define DPHY_TX_TIME0       (A64_DPHY_BASE + 0x010)
#define DPHY_TX_TIME1       (A64_DPHY_BASE + 0x014)
#define DPHY_TX_TIME2       (A64_DPHY_BASE + 0x018)
#define DPHY_TX_TIME3       (A64_DPHY_BASE + 0x01C)
#define DPHY_TX_TIME4       (A64_DPHY_BASE + 0x020)
#define DPHY_ANA0           (A64_DPHY_BASE + 0x040)
#define DPHY_ANA1           (A64_DPHY_BASE + 0x044)
#define DPHY_ANA2           (A64_DPHY_BASE + 0x048)
#define DPHY_ANA3           (A64_DPHY_BASE + 0x04C)
#define DPHY_ANA4           (A64_DPHY_BASE + 0x050)
#define DPHY_DBG_DATA       (A64_DPHY_BASE + 0x0F0)

/* ---- TCON0 registers ---- */
#define TCON0_CTL           (A64_TCON0_BASE + 0x000)
#define TCON0_DCLK          (A64_TCON0_BASE + 0x118)
#define TCON0_BASIC0        (A64_TCON0_BASE + 0x090)
#define TCON0_BASIC1        (A64_TCON0_BASE + 0x094)
#define TCON0_BASIC2        (A64_TCON0_BASE + 0x098)
#define TCON0_BASIC3        (A64_TCON0_BASE + 0x09C)
#define TCON0_BASIC4        (A64_TCON0_BASE + 0x0A0)
#define TCON0_BASIC5        (A64_TCON0_BASE + 0x0A4)
#define TCON_GCTRL          (A64_TCON0_BASE + 0x000)
#define TCON_GINT0          (A64_TCON0_BASE + 0x004)
#define TCON_GINT1          (A64_TCON0_BASE + 0x008)
#define TCON0_IO_TRI        (A64_TCON0_BASE + 0x08C)
#define TCON0_CPU_IF        (A64_TCON0_BASE + 0x060)

/* ---- XBD599 panel timing (1440×720, 60 Hz, 4 lanes) ---- */
#define PANEL_WIDTH         720
#define PANEL_HEIGHT        1440
#define PANEL_LANES         4
/* Horizontal timing (DSI byte clock units) */
#define PANEL_HSA           8
#define PANEL_HBP           20
#define PANEL_HFP           20
/* Vertical timing (lines) */
#define PANEL_VSA           4
#define PANEL_VBP           12
#define PANEL_VFP           18
/* MIPI byte clock: 500 MHz lane rate / 8 bits × 4 lanes = 250 MHz byte clock */
#define PANEL_LANE_RATE_MBPS 500
/* HLINE = (HSA + HBP + HACTIVE_bytes + HFP) in byte clocks
 * HACTIVE_bytes = ceil(720 * 3 / 4) * 4 = 540  (RGB888, 4-lane)
 * Packed: 720 * 24 / (4 * 8) = 540 byte-clock periods per line */
#define PANEL_HACTIVE_BC    540
#define PANEL_HLINE_TIME    (PANEL_HSA + PANEL_HBP + PANEL_HACTIVE_BC + PANEL_HFP)

/* DCS commands */
#define DCS_NOP             0x00
#define DCS_SWRESET         0x01
#define DCS_SLPOUT          0x11
#define DCS_DISPON          0x29
#define DCS_DISPOFF         0x28
#define DCS_SLPIN           0x10
#define DCS_CASET           0x2A
#define DCS_PASET           0x2B

/* ---- Forward declarations ---- */
static void a64_ccu_dsi_clk_enable(void);
static void a64_dphy_init(void);
static void a64_dsi_host_init(void);
static void a64_dsi_send_cmd(uint8_t type, const uint8_t* payload, uint8_t len);
static void xbd599_init_sequence(void);
static void a64_tcon0_init(void);
static void dsi_wait_fifo_empty(void);

/* ---- CCU: enable DSI + TCON0 clocks ---- */

static void a64_ccu_dsi_clk_enable(void)
{
    /* Enable MIPI PLL: target ~500 MHz for 500 Mbps/lane
     * PLL_MIPI = 24 MHz × N / M  (N=25, M=1 → 600 MHz; divide by video_div=1)
     * Bit 31: enable, bits 7:4: N (factor_n), bits 3:0 not used for MIPI */
    reg_write(CCU_PLL_MIPI_CTRL, (1U<<31)|(1U<<30)|(25U<<4));
    /* Wait for PLL lock (bit 28) */
    uint32_t to = 10000;
    while (to-- && !(reg_read(CCU_PLL_MIPI_CTRL) & (1U<<28)))
        ;

    /* MIPI DSI clock: source=PLL_MIPI (bit 24=1), div=1 */
    reg_write(CCU_MIPI_CLK, (1U<<31)|(1U<<24)|0);

    /* De-assert DSI reset (bit 1 of BUS_SOFT_RST0) */
    reg_set(CCU_BUS_SOFT_RST0, (1U<<1));

    /* Gate on MIPI DSI bus clock (bit 1 of BUS_CLK_GATE1) */
    reg_set(CCU_BUS_CLK_GATE1, (1U<<1));

    timer_delay_ms(1);
    printk(KERN_DEBUG "a64_dsi: CCU clocks enabled, PLL_MIPI=0x%08x\n",
           reg_read(CCU_PLL_MIPI_CTRL));
}

/* ---- D-PHY init ---- */

static void a64_dphy_init(void)
{
    /* Power down PHY, configure PLL, then power up */

    /* Assert reset */
    reg_write(DSI_PHY_RSTZ, 0x0);
    timer_delay_ms(1);

    /* Program D-PHY analog registers for 500 Mbps/lane.
     * Values derived from A64 user manual §7.7 and mainline Linux
     * drivers/phy/allwinner/phy-sun6i-mipi-dphy.c */

    /* ANA0: enable LDO, set Vref */
    reg_write(DPHY_ANA0, 0x03040000);
    /* ANA1: PLL charge pump, enable bandgap */
    reg_write(DPHY_ANA1, 0x84000000);
    timer_delay_ms(1);
    /* ANA2: enable PLL */
    reg_write(DPHY_ANA2, 0x03000000);
    timer_delay_ms(1);
    /* ANA3: enable data lanes (4 lanes: bits 0-3 + clk lane bit 4) */
    reg_write(DPHY_ANA3, 0x0F000000);
    /* ANA4: termination resistors on */
    reg_write(DPHY_ANA4, 0x1F011F00);

    /* TX timing registers for 500 Mbps/lane (byte_clk = 62.5 MHz).
     * All timings in byte-clock units per MIPI D-PHY spec v1.1. */
    /* TIME0: HS-PREPARE + HS-ZERO */
    reg_write(DPHY_TX_TIME0, (0x1f << 8) | 0x1f);
    /* TIME1: HS-TRAIL + HS-EXIT */
    reg_write(DPHY_TX_TIME1, (0x1f << 8) | 0x01);
    /* TIME2: CLK-PREPARE + CLK-ZERO */
    reg_write(DPHY_TX_TIME2, (0x06 << 8) | 0x28);
    /* TIME3: CLK-POST + CLK-PRE */
    reg_write(DPHY_TX_TIME3, (0x03 << 8) | 0x01);
    /* TIME4: LP→HS wakeup + CLK-TRAIL */
    reg_write(DPHY_TX_TIME4, (0xff << 8) | 0x05);

    /* Enable PHY: 4 data lanes + clock lane, TX mode */
    reg_write(DPHY_TX_CTL, (1U<<31) | (0xF << 4) | (1U << 0));

    /* Lift DSI PHY resets */
    reg_write(DSI_PHY_RSTZ, 0x00000007);  /* testclr=0, shutdownz=1, rstz=1, enableclk=1 */
    timer_delay_ms(1);

    /* Wait for PHY lock (phylock bit 0 + phystopstateclklane bit 2) */
    uint32_t to = 10000;
    while (to-- && (reg_read(DSI_PHY_STATUS) & 0x05) != 0x05)
        ;
    if (!to)
        printk(KERN_WARN "a64_dsi: D-PHY lock timeout (status=0x%08x)\n",
               reg_read(DSI_PHY_STATUS));
    else
        printk(KERN_DEBUG "a64_dsi: D-PHY locked\n");
}

/* ---- DSI host init ---- */

static void a64_dsi_host_init(void)
{
    /* Power down host */
    reg_write(DSI_PWR_UP, 0x0);

    /* Clock manager: TX escape = byte_clk / 8 = ~7.8 MHz (< 20 MHz max) */
    reg_write(DSI_CLKMGR_CFG, (8 << 8) | 8);

    /* Virtual channel 0 */
    reg_write(DSI_DPI_VCID, 0);

    /* 24-bit RGB888 colour coding */
    reg_write(DSI_DPI_COLOR_CODING, 0x5);

    /* Signal polarities: VSYNC active low, HSYNC active low, DE active high */
    reg_write(DSI_DPI_CFG_POL, 0x1);

    /* LP mode timing */
    reg_write(DSI_DPI_LP_CMD_TIM, (0x04 << 16) | 0x02);

    /* 4 lanes (N-1) */
    reg_write(DSI_PHY_IF_CFG, (PANEL_LANES - 1) | (0x02 << 8));

    /* Enable error correction (ECC + CRC) */
    reg_write(DSI_PCKHDL_CFG, (1U<<2)|(1U<<1)|(1U<<0));

    /* Video mode — burst mode */
    reg_write(DSI_MODE_CFG, 0x1);       /* 1 = video mode */
    reg_write(DSI_VID_MODE_CFG, (1U<<24)|(1U<<20)|(1U<<16)|(0x2));  /* burst + LP transitions allowed */

    /* Horizontal timing */
    reg_write(DSI_VID_PKT_SIZE,   PANEL_WIDTH);
    reg_write(DSI_VID_NUM_CHUNKS, 0);
    reg_write(DSI_VID_NULL_SIZE,  0);
    reg_write(DSI_VID_HSA_TIME,   PANEL_HSA);
    reg_write(DSI_VID_HBP_TIME,   PANEL_HBP);
    reg_write(DSI_VID_HLINE_TIME, PANEL_HLINE_TIME);

    /* Vertical timing */
    reg_write(DSI_VID_VSA_LINES,    PANEL_VSA);
    reg_write(DSI_VID_VBP_LINES,    PANEL_VBP);
    reg_write(DSI_VID_VFP_LINES,    PANEL_VFP);
    reg_write(DSI_VID_VACTIVE_LINES, PANEL_HEIGHT);

    /* LP clock: enable non-continuous mode */
    reg_write(DSI_LPCLK_CTRL, 0x1);

    /* PHY LP→HS and HS→LP timing (byte clock cycles) */
    reg_write(DSI_PHY_TMR_LPCLK, (0x28 << 16) | 0x0f);
    reg_write(DSI_PHY_TMR_CFG,   (0x40 << 16) | 0x0f);

    /* Disable all interrupts */
    reg_write(DSI_INT_MSK0, 0x0);
    reg_write(DSI_INT_MSK1, 0x0);

    /* Power up DSI host */
    reg_write(DSI_PWR_UP, 0x1);

    printk(KERN_DEBUG "a64_dsi: DSI host initialised, vid_hline=%u vactive=%u\n",
           PANEL_HLINE_TIME, PANEL_HEIGHT);
}

/* ---- DSI command transmission ---- */

static void dsi_wait_fifo_empty(void)
{
    /* Bits 1 (cmd_empty) and 5 (pld_r_empty) must be 1 */
    uint32_t to = 100000;
    while (to-- && !(reg_read(DSI_CMD_PKT_STATUS) & (1U<<1)))
        ;
}

/* Send a DCS short (1 or 2 byte) or long write command in LP mode */
static void a64_dsi_send_cmd(uint8_t type, const uint8_t* payload, uint8_t len)
{
    /* Switch to command mode */
    reg_write(DSI_MODE_CFG, 0x0);

    if (len <= 2) {
        /* Short packet */
        uint32_t hdr = (uint32_t)type |
                       ((len >= 1 ? payload[0] : 0U) << 8) |
                       ((len >= 2 ? payload[1] : 0U) << 16);
        dsi_wait_fifo_empty();
        reg_write(DSI_GEN_HDR, hdr);
    } else {
        /* Long packet: load payload FIFO first */
        dsi_wait_fifo_empty();
        uint32_t i = 0;
        while (i + 3 < len) {
            uint32_t word = (uint32_t)payload[i]   |
                            ((uint32_t)payload[i+1] << 8)  |
                            ((uint32_t)payload[i+2] << 16) |
                            ((uint32_t)payload[i+3] << 24);
            reg_write(DSI_GEN_PLD_DATA, word);
            i += 4;
        }
        /* Remaining bytes */
        if (i < len) {
            uint32_t word = 0;
            for (uint32_t j = 0; j < len - i; j++)
                word |= (uint32_t)payload[i+j] << (j * 8);
            reg_write(DSI_GEN_PLD_DATA, word);
        }
        /* Header: type + WC_LSB + WC_MSB */
        uint32_t hdr = (uint32_t)type | ((uint32_t)len << 8);
        reg_write(DSI_GEN_HDR, hdr);
    }

    dsi_wait_fifo_empty();

    /* Return to video mode */
    reg_write(DSI_MODE_CFG, 0x1);
}

/* ---- XBD599 init sequence ---- */
/*
 * The Xingbangda XBD599 uses a Sitronix ST7703 controller with vendor
 * extensions. Init sequence derived from PinePhone Pro mainline panel driver
 * (drivers/gpu/drm/panel/panel-sitronix-st7703.c, xbd599 variant).
 */
static void xbd599_init_sequence(void)
{
    uint8_t cmd[20];

    /* SETEXTC — Enable extended command set (page 0) */
    cmd[0]=0xF0; cmd[1]=0xC3; cmd[2]=0x96;
    a64_dsi_send_cmd(0x39, cmd, 3);   /* DCS long write */

    /* SETMIPI — MIPI config: 4 lanes, 500 Mbps, burst mode */
    cmd[0]=0xB0; cmd[1]=0x80;
    a64_dsi_send_cmd(0x15, cmd, 2);

    /* SETPOWER — VOP 4.6V, VGH 14V, VGL -10V */
    cmd[0]=0xB1; cmd[1]=0x21; cmd[2]=0x04; cmd[3]=0x02;
    cmd[4]=0x02; cmd[5]=0xAA; cmd[6]=0x11; cmd[7]=0x10;
    cmd[8]=0x55; cmd[9]=0xAA; cmd[10]=0x11; cmd[11]=0x10; cmd[12]=0x55;
    a64_dsi_send_cmd(0x39, cmd, 13);

    /* SETDISP — Normally black, 2 dot inversion, 720-line gate */
    cmd[0]=0xB2; cmd[1]=0x00; cmd[2]=0x02; cmd[3]=0x00;
    cmd[4]=0x80; cmd[5]=0x70; cmd[6]=0x11; cmd[7]=0x09;
    cmd[8]=0x03; cmd[9]=0x00; cmd[10]=0x00; cmd[11]=0x00;
    a64_dsi_send_cmd(0x39, cmd, 12);

    /* SETCYC — GOA timing */
    cmd[0]=0xB4; cmd[1]=0x00; cmd[2]=0xFF; cmd[3]=0x02;
    cmd[4]=0xC0; cmd[5]=0x02; cmd[6]=0xC0; cmd[7]=0x00;
    cmd[8]=0x00; cmd[9]=0x08; cmd[10]=0x00; cmd[11]=0x04;
    a64_dsi_send_cmd(0x39, cmd, 12);

    /* SETGIP1 — GOA input signal mapping */
    cmd[0]=0xE0; cmd[1]=0x00; cmd[2]=0x07; cmd[3]=0x11;
    cmd[4]=0x0B; cmd[5]=0x10; cmd[6]=0x07; cmd[7]=0x06;
    cmd[8]=0x07; cmd[9]=0x0B; cmd[10]=0x0F; cmd[11]=0x05;
    cmd[12]=0x08; cmd[13]=0x0D; cmd[14]=0x11; cmd[15]=0x0F;
    cmd[16]=0x08; cmd[17]=0x04; cmd[18]=0x12; cmd[19]=0x04;
    a64_dsi_send_cmd(0x39, cmd, 20);

    /* SETGIP2 — GOA output signal mapping (negative gamma) */
    cmd[0]=0xE1; cmd[1]=0x00; cmd[2]=0x07; cmd[3]=0x11;
    cmd[4]=0x0B; cmd[5]=0x10; cmd[6]=0x07; cmd[7]=0x06;
    cmd[8]=0x07; cmd[9]=0x0B; cmd[10]=0x0F; cmd[11]=0x05;
    cmd[12]=0x08; cmd[13]=0x0D; cmd[14]=0x11; cmd[15]=0x0F;
    cmd[16]=0x08; cmd[17]=0x04; cmd[18]=0x12; cmd[19]=0x04;
    a64_dsi_send_cmd(0x39, cmd, 20);

    /* SETRGBIF — RGB interface control: SDO polarity */
    cmd[0]=0xB5; cmd[1]=0x08; cmd[2]=0x09;
    a64_dsi_send_cmd(0x39, cmd, 3);

    /* SETSCR — Source output: gamma correction, RGB order */
    cmd[0]=0xC0; cmd[1]=0x80; cmd[2]=0x55;
    a64_dsi_send_cmd(0x39, cmd, 3);

    /* SETVDC — VDC setting */
    cmd[0]=0xBC; cmd[1]=0x01;
    a64_dsi_send_cmd(0x15, cmd, 2);

    /* Exit sleep: SLP_OUT — allow 120 ms wakeup per MIPI DCS spec */
    cmd[0]=DCS_SLPOUT;
    a64_dsi_send_cmd(0x05, cmd, 1);
    timer_delay_ms(120);

    /* Display on */
    cmd[0]=DCS_DISPON;
    a64_dsi_send_cmd(0x05, cmd, 1);
    timer_delay_ms(20);

    printk(KERN_INFO "a64_dsi: XBD599 panel init complete\n");
}

/* ---- TCON0 init ---- */

static void a64_tcon0_init(void)
{
    /* Enable TCON, select TCON0 for DSI output */
    reg_write(TCON_GCTRL, (1U<<31));

    /* DCLK: pixel clock = lane_rate / 8 × lanes / bpp × pixel_width
     *   = 500e6/8 × 4 / 24 × 720 ≈ 75 MHz → div = CCU_MIPI/75MHz ~ 8 */
    reg_write(TCON0_DCLK, (0xF << 28) | 8);  /* F=enable, div=8 */

    /* Active area */
    reg_write(TCON0_BASIC0, ((PANEL_WIDTH  - 1) << 16) | (PANEL_HEIGHT - 1));

    /* Horizontal: HBP, total */
    reg_write(TCON0_BASIC1, ((PANEL_HBP + PANEL_HSA - 1) << 16) |
                             (PANEL_HSA + PANEL_HBP + PANEL_WIDTH + PANEL_HFP - 1));

    /* Vertical: VBP, total */
    reg_write(TCON0_BASIC2, ((PANEL_VBP + PANEL_VSA - 1) << 16) |
                             (PANEL_VSA + PANEL_VBP + PANEL_HEIGHT + PANEL_VFP - 1));

    /* HSYNC + VSYNC widths */
    reg_write(TCON0_BASIC3, ((PANEL_HSA - 1) << 16) | (PANEL_VSA - 1));

    /* Enable IO pins, de-assert IO tri-state */
    reg_write(TCON0_IO_TRI, 0x00000000);

    printk(KERN_DEBUG "a64_dsi: TCON0 configured %ux%u\n",
           PANEL_WIDTH, PANEL_HEIGHT);
}

/* ---- Public init function, called from display_pipeline_init() ---- */

int display_a64_dsi_init(void)
{
    printk(KERN_INFO "a64_dsi: initializing PinePhone Pro display\n");

    a64_ccu_dsi_clk_enable();
    a64_tcon0_init();
    a64_dphy_init();
    a64_dsi_host_init();
    xbd599_init_sequence();

    printk(KERN_INFO "a64_dsi: %dx%d @ 60Hz, 4-lane DSI, ready\n",
           PANEL_WIDTH, PANEL_HEIGHT);
    return 0;
}

void display_a64_dsi_shutdown(void)
{
    uint8_t cmd[1];
    cmd[0] = DCS_DISPOFF;
    a64_dsi_send_cmd(0x05, cmd, 1);
    timer_delay_ms(20);
    cmd[0] = DCS_SLPIN;
    a64_dsi_send_cmd(0x05, cmd, 1);
    timer_delay_ms(80);
    reg_write(DSI_PWR_UP, 0x0);
    reg_write(DPHY_TX_CTL, 0x0);
}
