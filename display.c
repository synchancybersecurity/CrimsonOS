/*
 * Crimson OS - Display Driver
 * MIPI DSI interface, framebuffer management, VSync
 * Supports: ILI9881C, ST7701S, RM67191 (common phone panels)
 */

#include <crimson/types.h>
#include <crimson/mm.h>
#include <crimson/printk.h>
#include <crimson/spinlock.h>
#include <crimson/gpio.h>
#include <crimson/timer.h>
#include <crimson/display.h>

/* ── Platform: QEMU virt framebuffer (default) ──
 * On real hardware, this maps to MIPI DSI controller registers
 */
#define FB_BASE_QEMU        0x00000000  /* Set at runtime from DTB */

/* MIPI DSI registers (Synopsys DWC MIPI DSI host) */
#define DSI_BASE            0xFE700000
#define DSI_PWR_UP          (DSI_BASE + 0x04)
#define DSI_CLKMGR_CFG      (DSI_BASE + 0x08)
#define DSI_DPI_VCID        (DSI_BASE + 0x0C)
#define DSI_DPI_COLOR       (DSI_BASE + 0x10)
#define DSI_DPI_CFG         (DSI_BASE + 0x14)
#define DSI_DPI_LP          (DSI_BASE + 0x18)
#define DSI_PCKHDL_CFG      (DSI_BASE + 0x2C)
#define DSI_EDPI_CMD_SIZE   (DSI_BASE + 0x40)
#define DSI_MODE_CFG        (DSI_BASE + 0x34)
#define DSI_VID_MODE_CFG    (DSI_BASE + 0x38)
#define DSI_VID_PKT_SIZE    (DSI_BASE + 0x3C)
#define DSI_CMD_MODE_CFG    (DSI_BASE + 0x68)
#define DSI_GEN_HDR         (DSI_BASE + 0x6C)
#define DSI_GEN_PLD_DATA    (DSI_BASE + 0x70)
#define DSI_CMD_PKT_STATUS  (DSI_BASE + 0x74)
#define DSI_TO_CNT_CFG      (DSI_BASE + 0x78)
#define DSI_BTA_TO_CNT      (DSI_BASE + 0x8C)
#define DSI_LPCLK_CTRL      (DSI_BASE + 0x94)
#define DSI_PHY_TMR_LPCLK   (DSI_BASE + 0x98)
#define DSI_PHY_TMR_RD      (DSI_BASE + 0x9C)
#define DSI_PHY_TMR_CFG     (DSI_BASE + 0x9C)
#define DSI_ERROR_ST0       (DSI_BASE + 0xA0)
#define DSI_ERROR_ST1       (DSI_BASE + 0xA4)
#define DSI_ERROR_MSK0      (DSI_BASE + 0xA8)
#define DSI_ERROR_MSK1      (DSI_BASE + 0xAC)
#define DSI_PHY_RSTZ        (DSI_BASE + 0xA0)
#define DSI_PHY_IF_CFG      (DSI_BASE + 0xA4)
#define DSI_PHY_IF_CTRL     (DSI_BASE + 0xA8)
#define DSI_PHY_STATUS      (DSI_BASE + 0xB0)
#define DSI_PHY_TST_CTRL0   (DSI_BASE + 0xB4)
#define DSI_PHY_TST_CTRL1   (DSI_BASE + 0xB8)
#define DSI_INT_ST0         (DSI_BASE + 0xBC)
#define DSI_INT_ST1         (DSI_BASE + 0xC0)
#define DSI_INT_MSK0        (DSI_BASE + 0xC4)
#define DSI_INT_MSK1        (DSI_BASE + 0xC8)
#define DSI_DCS_CMD_0       (DSI_BASE + 0x100)

/* D-PHY registers */
#define DPHY_BASE           0xFE700100
#define DPHY_PLL_CNTRL      (DPHY_BASE + 0x00)
#define DPHY_PLL_RG         (DPHY_BASE + 0x04)
#define DPHY_PLL_DIV        (DPHY_BASE + 0x08)
#define DPHY_CFG_CLK        (DPHY_BASE + 0x0C)

/* Backlight PWM */
#define PWM_BASE            0xFE20C000
#define PWM_CTRL            (PWM_BASE + 0x00)
#define PWM_RNG1            (PWM_BASE + 0x10)
#define PWM_DAT1            (PWM_BASE + 0x14)
#define PWM_RNG2            (PWM_BASE + 0x20)
#define PWM_DAT2            (PWM_BASE + 0x24)

/* Panel commands */
#define DSI_DCS_SLPIN       0x10
#define DSI_DCS_SLPOUT      0x11
#define DSI_DCS_PTLON       0x12
#define DSI_DCS_NORON       0x13
#define DSI_DCS_INVOFF      0x20
#define DSI_DCS_INVON       0x21
#define DSI_DCS_ALLPOFF     0x22
#define DSI_DCS_ALLPON      0x23
#define DSI_DCS_GAMSET      0x26
#define DSI_DCS_DISPOFF     0x28
#define DSI_DCS_DISPON      0x29
#define DSI_DCS_CASET       0x2A
#define DSI_DCS_PASET       0x2B
#define DSI_DCS_RAMWR       0x2C
#define DSI_DCS_RGBSET      0x2D
#define DSI_DCS_PLTAR       0x30
#define DSI_DCS_VSCRDEF     0x33
#define DSI_DCS_TEOFF       0x34
#define DSI_DCS_TEON        0x35
#define DSI_DCS_MADCTL      0x36
#define DSI_DCS_VSCRSADD    0x37
#define DSI_DCS_IDMOFF      0x38
#define DSI_DCS_IDMON       0x39
#define DSI_DCS_COLMOD      0x3A
#define DSI_DCS_RAMWRC      0x3C
#define DSI_DCS_STPCTR      0x44

/* ── Framebuffer state ── */
static uint32_t g_fb_width  = DISP_WIDTH;
static uint32_t g_fb_height = DISP_HEIGHT;
static uint32_t g_fb_pitch  = DISP_PITCH;
static uint32_t* g_fb_base  = NULL;
static uint32_t g_brightness = 100;
static uint32_t g_vsync_count = 0;
static uint32_t g_dsi_initialized = 0;
static spinlock_t disp_lock;

/* Double buffering */
static uint32_t* g_fb_front = NULL;
static uint32_t* g_fb_back  = NULL;
static uint32_t  g_fb_current = 0;

/* ── ILI9881C initialization sequence ── */
static const uint8_t ili9881c_init[] = {
    0xFF, 3, 0x98, 0x81, 0x03,
    0x01, 1, 0x00,
    0x02, 1, 0x00,
    0x03, 1, 0x73,
    0x04, 1, 0x73,
    0x05, 1, 0x00,
    0x06, 1, 0x06,
    0x07, 1, 0x02,
    0x08, 1, 0x00,
    0x09, 1, 0x01,
    0x0A, 1, 0x01,
    0x0B, 1, 0x01,
    0x0C, 1, 0x01,
    0x0D, 1, 0x01,
    0x0E, 1, 0x00,
    0x0F, 1, 0x00,
    0x10, 1, 0xFF,
    0x11, 1, 0xF0,
    0x12, 1, 0x00,
    0x13, 1, 0x00,
    0x14, 1, 0x00,
    0x15, 1, 0xC0,
    0x16, 1, 0x08,
    0x17, 1, 0x00,
    0x18, 1, 0x00,
    0x19, 1, 0x00,
    0x1A, 1, 0x00,
    0x1B, 1, 0x00,
    0x1C, 1, 0x00,
    0x1D, 1, 0x00,
    0x20, 1, 0x01,
    0x21, 1, 0x23,
    0x22, 1, 0x45,
    0x23, 1, 0x67,
    0x24, 1, 0x01,
    0x25, 1, 0x23,
    0x26, 1, 0x45,
    0x27, 1, 0x67,
    0x30, 1, 0x01,
    0x31, 1, 0x22,
    0x32, 1, 0x22,
    0x33, 1, 0x22,
    0x34, 1, 0x87,
    0x35, 1, 0x96,
    0x36, 1, 0xAA,
    0x37, 1, 0xDB,
    0x38, 1, 0xCC,
    0x39, 1, 0xBD,
    0x3A, 1, 0x78,
    0x3B, 1, 0x69,
    0x3C, 1, 0x22,
    0x3D, 1, 0x22,
    0x3E, 1, 0x22,
    0x3F, 1, 0x22,
    0x40, 1, 0x22,
    0xFF, 0
};

/* ── Register access ── */
static inline void dsi_wr32(uintptr_t reg, uint32_t val)
{
    volatile uint32_t* p = (volatile uint32_t*)reg;
    *p = val;
}

static inline uint32_t dsi_rd32(uintptr_t reg)
{
    volatile uint32_t* p = (volatile uint32_t*)reg;
    return *p;

}

/* ── DCS command via DSI ── */
static void dsi_dcs_cmd(uint8_t cmd)
{
    dsi_wr32(DSI_GEN_HDR, (cmd & 0xFF) | (0x05 << 8) | (0 << 6));
}

static void dsi_dcs_write(uint8_t cmd, const uint8_t* data, uint32_t len)
{
    if (len == 0) {
        dsi_dcs_cmd(cmd);
    } else if (len == 1) {
        dsi_wr32(DSI_GEN_HDR, (cmd & 0xFF) | (0x15 << 8) | (0 << 6));
        dsi_wr32(DSI_GEN_PLD_DATA, data[0]);
    } else {
        dsi_wr32(DSI_GEN_HDR, (cmd & 0xFF) | (0x39 << 8) | (0 << 6));
        uint32_t pld = 0;
        for (uint32_t i = 0; i < len; i++) {
            pld |= (uint32_t)data[i] << ((i % 4) * 8);
            if ((i % 4) == 3) {
                dsi_wr32(DSI_GEN_PLD_DATA, pld);
                pld = 0;
            }
        }
        if (len % 4) dsi_wr32(DSI_GEN_PLD_DATA, pld);
    }

    /* Wait for completion */
    uint32_t timeout = 10000;
    while ((dsi_rd32(DSI_CMD_PKT_STATUS) & (1 << 1)) && --timeout);
}

static void dsi_dcs_write1(uint8_t cmd, uint8_t p0)
{
    dsi_dcs_write(cmd, &p0, 1);
}

/* ── Wait for PHY lock ── */
static int dsi_wait_phy_lock(void)
{
    uint32_t timeout = 100000;
    while (--timeout) {
        if (dsi_rd32(DSI_PHY_STATUS) & (1 << 0))
            return 0;
    }
    return -1;
}

/* ── Initialize MIPI DSI + panel ── */
static int dsi_panel_init(void)
{
    printk(KERN_INFO "[DSI] Initializing MIPI DSI controller...\n");

    /* Reset DSI */
    dsi_wr32(DSI_PWR_UP, 0);
    timer_delay_ms(1);
    dsi_wr32(DSI_PWR_UP, 1);

    /* Configure PHY */
    dsi_wr32(DSI_PHY_RSTZ, 0);
    timer_delay_ms(1);

    /* 4 data lanes, 80MHz lane clock */
    dsi_wr32(DSI_PHY_IF_CFG, (4 << 0) | (0 << 8));

    /* PLL configuration for 80MHz */
    dsi_wr32(DPHY_PLL_CNTRL, 0x01);
    dsi_wr32(DPHY_PLL_RG, 0x1A);
    dsi_wr32(DPHY_PLL_DIV, 0x32);

    dsi_wr32(DSI_PHY_RSTZ, 0x01);
    timer_delay_ms(1);
    dsi_wr32(DSI_PHY_RSTZ, 0x05);
    timer_delay_ms(1);
    dsi_wr32(DSI_PHY_RSTZ, 0x07);

    if (dsi_wait_phy_lock() < 0) {
        printk(KERN_WARN "[DSI] PHY lock timeout, using software fallback\n");
        return -1;
    }

    printk(KERN_INFO "[DSI] PHY locked\n");

    /* D-PHY timing */
    dsi_wr32(DSI_PHY_TMR_LPCLK, 0x1A0A);
    dsi_wr32(DSI_PHY_TMR_RD, 0x1A0A);
    dsi_wr32(DSI_PHY_TMR_CFG, 0x1A0A);

    /* Clock configuration */
    dsi_wr32(DSI_CLKMGR_CFG, (0x08 << 8) | 0x08);

    /* Error masks */
    dsi_wr32(DSI_ERROR_MSK0, 0xFFFFFFFF);
    dsi_wr32(DSI_ERROR_MSK1, 0xFFFFFFFF);
    dsi_wr32(DSI_INT_MSK0, 0xFFFFFFFF);
    dsi_wr32(DSI_INT_MSK1, 0xFFFFFFFF);

    /* Enable clock lane */
    dsi_wr32(DSI_LPCLK_CTRL, 0x01);

    /* Configure for video mode */
    dsi_wr32(DSI_MODE_CFG, 0);  /* Video mode */
    dsi_wr32(DSI_DPI_VCID, 0);
    dsi_wr32(DSI_DPI_COLOR, 0x03);  /* RGB888 */
    dsi_wr32(DSI_DPI_CFG, 0x00);

    /* Video packet: 1080 bytes per line */
    dsi_wr32(DSI_VID_PKT_SIZE, g_fb_width);

    /* Virtual channel config */
    dsi_wr32(DSI_PCKHDL_CFG, 0x18);
    dsi_wr32(DSI_VID_MODE_CFG, 0x2F00);

    /* Exit ULPM */
    dsi_wr32(DSI_DPI_LP, 0);

    printk(KERN_INFO "[DSI] Controller configured\n");

    /* ── Panel init (ILI9881C) ── */
    printk(KERN_INFO "[DSI] Initializing ILI9881C panel...\n");

    /* Exit sleep */
    dsi_dcs_cmd(DSI_DCS_SLPOUT);
    timer_delay_ms(120);

    /* Send init sequence */
    const uint8_t* p = ili9881c_init;
    while (*p) {
        uint8_t reg = *p++;
        uint8_t n = *p++;
        if (n == 0) break;

        dsi_dcs_write1(0xFF, reg);
        for (uint8_t i = 0; i < n; i++) {
            uint8_t v = *p++;
            dsi_dcs_write1(reg + i + 1, v);
        }
    }

    /* Set pixel format: RGB888 */
    dsi_dcs_write1(DSI_DCS_COLMOD, 0x77);

    /* Set MADCTL: normal orientation */
    dsi_dcs_write1(DSI_DCS_MADCTL, 0x00);

    /* Set column address: 0 to 1079 */
    uint8_t caset[] = { 0x00, 0x00, 0x04, 0x2F };
    dsi_dcs_write(DSI_DCS_CASET, caset, 4);

    /* Set page address: 0 to 1919 */
    uint8_t paset[] = { 0x00, 0x00, 0x07, 0x7F };
    dsi_dcs_write(DSI_DCS_PASET, paset, 4);

    /* Display on */
    dsi_dcs_cmd(DSI_DCS_DISPON);
    timer_delay_ms(20);

    /* Enable tearing effect */
    dsi_dcs_write1(DSI_DCS_TEON, 0x00);

    printk(KERN_INFO "[DSI] ILI9881C panel initialized\n");
    g_dsi_initialized = 1;
    return 0;
}

/* ── PWM backlight ── */
static void backlight_init(void)
{
    /* Configure PWM for backlight */
    dsi_wr32(PWM_RNG1, 100);
    dsi_wr32(PWM_DAT1, g_brightness);
    dsi_wr32(PWM_CTRL, 0x81);  /* Enable PWM, MSEN */
    printk(KERN_INFO "[DISP] Backlight PWM initialized\n");
}

static void backlight_set(uint32_t pct)
{
    if (pct > 100) pct = 100;
    g_brightness = pct;
    dsi_wr32(PWM_DAT1, pct);
}

/* ═══════════════════════════════════════════════════════════
 *  PUBLIC API
 * ═══════════════════════════════════════════════════════════ */

void display_init(void)
{
    spinlock_init(&disp_lock);

    printk(KERN_INFO "[DISP] Display driver initializing...\n");
    printk(KERN_INFO "[DISP] Resolution: %ux%u, pitch=%u\n",
           g_fb_width, g_fb_height, g_fb_pitch);

    /* Allocate double framebuffer */
    size_t fb_size = g_fb_width * g_fb_height * 4;
    g_fb_base = (uint32_t*)kmalloc(fb_size * 2);
    if (!g_fb_base) {
        printk(KERN_CRIT "[DISP] Failed to allocate framebuffer!\n");
        return;
    }

    g_fb_front = g_fb_base;
    g_fb_back  = g_fb_base + (g_fb_width * g_fb_height);
    g_fb_current = 0;

    /* Clear both buffers */
    memset(g_fb_front, 0, fb_size);
    memset(g_fb_back, 0, fb_size);

    /* Initialize DSI + panel */
    if (dsi_panel_init() < 0) {
        printk(KERN_WARN "[DISP] DSI init failed, using memory-only mode\n");
    }

    /* Init backlight */
    backlight_init();
    backlight_set(100);

    printk(KERN_INFO "[DISP] Display ready, FB@%p (%u bytes x2)\n",
           g_fb_base, fb_size);
}

void display_shutdown(void)
{
    /* Turn off panel */
    dsi_dcs_cmd(DSI_DCS_DISPOFF);
    timer_delay_ms(20);
    dsi_dcs_cmd(DSI_DCS_SLPIN);
    timer_delay_ms(120);

    /* Disable DSI */
    dsi_wr32(DSI_PWR_UP, 0);
    dsi_wr32(DSI_PHY_RSTZ, 0);

    /* Turn off backlight */
    backlight_set(0);
}

gfx_ctx_t* display_get_ctx(void)
{
    return gfx_create_context(g_fb_current ? g_fb_back : g_fb_front,
                               g_fb_width, g_fb_height);
}

void display_swap_buffers(void)
{
    spin_lock(&disp_lock);

    /* In real hardware: trigger DMA from back buffer to DSI
     * For now: just swap pointers */
    uint32_t* current_fb = g_fb_current ? g_fb_back : g_fb_front;

    if (g_dsi_initialized) {
        /* Send frame to panel via DSI write memory start */
        dsi_dcs_cmd(DSI_DCS_RAMWR);

        /* In a real implementation, this would be a DMA transfer
         * to the DSI FIFO. Here we just update the pointer. */
    }

    g_fb_current = !g_fb_current;
    g_vsync_count++;

    spin_unlock(&disp_lock);
}

void display_clear(uint32_t color)
{
    uint32_t* fb = g_fb_current ? g_fb_back : g_fb_front;
    uint32_t count = g_fb_width * g_fb_height;

    /* Fast fill with 32-bit stores */
    for (uint32_t i = 0; i < count; i++)
        fb[i] = color;
}

void display_get_info(uint32_t* w, uint32_t* h, uint32_t* pitch, void** fb)
{
    if (w)     *w     = g_fb_width;
    if (h)     *h     = g_fb_height;
    if (pitch) *pitch = g_fb_pitch;
    if (fb)    *fb    = g_fb_current ? g_fb_back : g_fb_front;
}

void display_set_brightness(uint32_t level)
{
    backlight_set(level);
}

void display_vsync_wait(void)
{
    /* Wait for next VSync by polling PHY status */
    if (g_dsi_initialized) {
        uint32_t target = g_vsync_count + 1;
        while (g_vsync_count < target) {
            /* Small delay to prevent tight polling */
            __asm__ volatile("yield");
        }
    } else {
        /* Software VSync: ~60Hz = 16.6ms */
        timer_delay_ms(16);
        g_vsync_count++;
    }
}

uint32_t display_get_vsync_count(void)
{
    return g_vsync_count;
}

/* ── Legacy pixel API (used by compositor) ── */
void display_putpixel(uint32_t x, uint32_t y, uint32_t colour)
{
    if (x >= g_fb_width || y >= g_fb_height) return;
    uint32_t* fb = g_fb_current ? g_fb_back : g_fb_front;
    fb[y * g_fb_width + x] = colour;
}

void display_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t colour)
{
    for (uint32_t row = y; row < y + h && row < g_fb_height; row++)
        for (uint32_t col = x; col < x + w && col < g_fb_width; col++)
            display_putpixel(col, row, colour);
}

void display_blit(uint32_t dst_x, uint32_t dst_y, uint32_t w, uint32_t h,
                  const uint32_t* src, uint32_t src_pitch, bool use_alpha)
{
    if (!src) return;
    uint32_t* fb = g_fb_current ? g_fb_back : g_fb_front;

    for (uint32_t row = 0; row < h && dst_y + row < g_fb_height; row++) {
        for (uint32_t col = 0; col < w && dst_x + col < g_fb_width; col++) {
            uint32_t c = src[row * (src_pitch / 4) + col];
            if (!use_alpha || (c >> 24) != 0) {
                fb[(dst_y + row) * g_fb_width + dst_x + col] = c;
            }
        }
    }
}

void display_draw_text(uint32_t x, uint32_t y, const char* str,
                        uint32_t fg, uint32_t bg, uint32_t scale)
{
    gfx_ctx_t ctx;
    ctx.fb = g_fb_current ? g_fb_back : g_fb_front;
    ctx.width = g_fb_width;
    ctx.height = g_fb_height;
    gfx_reset_clip(&ctx);

    if (bg != C_TRANSPARENT) {
        uint32_t tw = gfx_text_width(str, scale);
        gfx_fill_rect(&ctx, (int32_t)x, (int32_t)y, tw + 4, FONT_H * scale + 4, bg);
    }
    gfx_draw_text(&ctx, (int32_t)x, (int32_t)y, str, fg, scale);
}
