/* Copyright (c) 2026 SynChanCyberSecurity LLC. All Rights Reserved. */
/*
 * Crimson OS - Display Subsystem Header
 * Framebuffer management, graphics context, 2D primitives
 */

#ifndef _CRIMSON_DISPLAY_H
#define _CRIMSON_DISPLAY_H

#include <crimson/types.h>

/* ── Screen dimensions (1080x1920 @ 440dpi typical) ── */
#define DISP_WIDTH              1080
#define DISP_HEIGHT             1920
#define DISP_PITCH              (DISP_WIDTH * 4)
#define DISP_FB_SIZE            (DISP_WIDTH * DISP_HEIGHT * 4)

/* ── Color format: 0xAARRGGBB ── */
#define CRGBA(r,g,b,a)          ((((uint32_t)(a))<<24)|(((uint32_t)(r))<<16)|(((uint32_t)(g))<<8)|((uint32_t)(b)))
#define CRGB(r,g,b)             CRGBA(r,g,b,255)
#define C_BLACK                 CRGB(0,0,0)
#define C_WHITE                 CRGB(255,255,255)
#define C_RED                   CRGB(239,68,68)
#define C_GREEN                 CRGB(34,197,94)
#define C_BLUE                  CRGB(59,130,246)
#define C_YELLOW                CRGB(234,179,8)
#define C_CYAN                  CRGB(6,182,212)
#define C_MAGENTA               CRGB(236,72,153)
#define C_ORANGE                CRGB(249,115,22)
#define C_PURPLE                CRGB(168,85,247)
#define C_PINK                  CRGB(236,72,153)
#define C_GRAY                  CRGB(128,128,128)
#define C_DKGRAY                CRGB(64,64,64)
#define C_LTGRAY                CRGB(200,200,200)
#define C_TRANSPARENT           0x00000000

/* Crimson OS brand palette */
#define C_CRIMSON               CRGB(220,20,60)
#define C_CRIMSON_DARK          CRGB(139,0,0)
#define C_BLOODMOON_BG          CRGB(10,5,15)
#define C_BLOODMOON_ACCENT      CRGB(180,40,60)
#define C_BLOODMOON_PANEL       CRGB(20,12,28)
#define C_BLOODMOON_TEXT        CRGB(230,220,240)
#define C_BLOODMOON_TEXT2       CRGB(160,150,170)

/* ── Blending modes ── */
#define BLEND_OPAQUE            0
#define BLEND_ALPHA             1
#define BLEND_ADD               2
#define BLEND_MUL               3

/* ── Graphics context ── */
typedef struct gfx_ctx {
    uint32_t*   fb;
    uint32_t    width;
    uint32_t    height;
    uint32_t    pitch;
    uint32_t    clip_x;
    uint32_t    clip_y;
    uint32_t    clip_x2;
    uint32_t    clip_y2;
    uint32_t    blend_mode;
} gfx_ctx_t;

/* ── Gradient ── */
typedef struct {
    uint32_t    color1;
    uint32_t    color2;
    uint32_t    vertical;   /* 1=top-to-bottom, 0=left-to-right */
} gradient_t;

/* ── Display API ── */
void        display_init(void);
void        display_shutdown(void);
gfx_ctx_t*  display_get_ctx(void);
void        display_swap_buffers(void);
void        display_clear(uint32_t color);
void        display_vsync_wait(void);
uint32_t    display_get_vsync_count(void);
void        display_set_brightness(uint32_t pct);
void        display_get_info(uint32_t* w, uint32_t* h, uint32_t* pitch, void** fb);

/* ── Graphics context ── */
gfx_ctx_t*  gfx_create_context(uint32_t* fb, uint32_t w, uint32_t h);
void        gfx_set_clip(gfx_ctx_t* ctx, uint32_t x, uint32_t y, uint32_t w, uint32_t h);
void        gfx_reset_clip(gfx_ctx_t* ctx);

/* ── Pixel / primitive ── */
void        gfx_putpixel(gfx_ctx_t* ctx, int32_t x, int32_t y, uint32_t c);
void        gfx_line(gfx_ctx_t* ctx, int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t c);
void        gfx_rect(gfx_ctx_t* ctx, int32_t x, int32_t y, uint32_t w, uint32_t h, uint32_t c);
void        gfx_fill_rect(gfx_ctx_t* ctx, int32_t x, int32_t y, uint32_t w, uint32_t h, uint32_t c);
void        gfx_rounded_rect(gfx_ctx_t* ctx, int32_t x, int32_t y, uint32_t w, uint32_t h, uint32_t r, uint32_t c);
void        gfx_fill_rounded(gfx_ctx_t* ctx, int32_t x, int32_t y, uint32_t w, uint32_t h, uint32_t r, uint32_t c);
void        gfx_circle(gfx_ctx_t* ctx, int32_t cx, int32_t cy, uint32_t r, uint32_t c);
void        gfx_fill_circle(gfx_ctx_t* ctx, int32_t cx, int32_t cy, uint32_t r, uint32_t c);
void        gfx_triangle(gfx_ctx_t* ctx, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint32_t c);
void        gfx_fill_triangle(gfx_ctx_t* ctx, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint32_t c);

/* ── Gradients ── */
void        gfx_gradient_rect(gfx_ctx_t* ctx, int32_t x, int32_t y, uint32_t w, uint32_t h, gradient_t* grad);
void        gfx_gradient_rounded(gfx_ctx_t* ctx, int32_t x, int32_t y, uint32_t w, uint32_t h, uint32_t r, gradient_t* grad);

/* ── Text ── */
void        gfx_draw_text(gfx_ctx_t* ctx, int32_t x, int32_t y, const char* str, uint32_t color, uint32_t scale);
void        gfx_draw_text_center(gfx_ctx_t* ctx, int32_t x, int32_t y, const char* str, uint32_t color, uint32_t scale);
void        gfx_draw_text_right(gfx_ctx_t* ctx, int32_t x, int32_t y, const char* str, uint32_t color, uint32_t scale);
uint32_t    gfx_text_width(const char* str, uint32_t scale);

/* ── Blit ── */
void        gfx_blit(gfx_ctx_t* ctx, int32_t x, int32_t y, uint32_t w, uint32_t h, const uint32_t* src);
void        gfx_blit_key(gfx_ctx_t* ctx, int32_t x, int32_t y, uint32_t w, uint32_t h, const uint32_t* src, uint32_t key);
void        gfx_blit_scaled(gfx_ctx_t* ctx, int32_t dx, int32_t dy, uint32_t dw, uint32_t dh, const uint32_t* src, uint32_t sw, uint32_t sh);
void        gfx_blit_alpha(gfx_ctx_t* ctx, int32_t x, int32_t y, uint32_t w, uint32_t h, const uint32_t* src);

/* ── Utility ── */
uint32_t    gfx_blend(uint32_t dst, uint32_t src);
uint32_t    gfx_color_lerp(uint32_t a, uint32_t b, uint8_t t);
uint32_t    gfx_darken(uint32_t c, uint8_t amt);
uint32_t    gfx_lighten(uint32_t c, uint8_t amt);

void display_blit(uint32_t dx, uint32_t dy, uint32_t w, uint32_t h,
                  const uint32_t* src, uint32_t stride, bool use_alpha);

/* Font metrics used by graphics and widget layers */
#ifndef FONT_W
#define FONT_W  8
#define FONT_H  16
#endif

#endif
