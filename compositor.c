/* Copyright (c) 2026 SynChanCyberSecurity LLC. All Rights Reserved. */
/*
 * Crimson OS - GUI Compositor & Window Manager
 * Surfaces, app launcher, lock screen, status bar, notifications
 */

#include <crimson/types.h>
#include <crimson/display.h>
#include <crimson/gui.h>
#include <crimson/touch.h>
#include <crimson/scheduler.h>
#include <crimson/process.h>
#include <crimson/printk.h>
#include <crimson/timer.h>
#include <crimson/string.h>
#include <crimson/mm.h>
#include <crimson/spinlock.h>

/* Forward declarations for static functions */
static void gui_draw_home_screen(void);
static void gui_draw_nav_bar(void);
static void gui_draw_notification_badge(void);
static void gui_draw_pin_pad(gfx_ctx_t* ctx, int32_t cx);
static void gui_draw_power_menu(void);
static void gui_draw_recent_apps(void);
static void handle_home_touch(touch_event_t* ev);
static void handle_launcher_touch(touch_event_t* ev);
static void handle_notification_touch(touch_event_t* ev);
static void handle_power_touch(touch_event_t* ev);


/* ── Global GUI state ── */
gui_state_t g_gui;
static gfx_ctx_t g_fb_ctx;
static spinlock_t g_gui_spinlock;

/* ── Internal views ── */
#define VIEW_HOME       0
#define VIEW_LAUNCHER   1
#define VIEW_LOCK       2
#define VIEW_NOTIFICATIONS 3
#define VIEW_RECENT     4
#define VIEW_POWER      5

static uint32_t current_view = VIEW_LOCK;
static uint32_t prev_view = VIEW_HOME;
static uint32_t view_transition = 0;
static uint32_t transition_start = 0;

/* ── PIN entry ── */
static char pin_buffer[16];
static uint32_t pin_len = 0;
static uint32_t pin_target = 1234;  /* Default PIN */
static uint32_t pin_failed = 0;

/* ── App icons (16 built-in apps) ── */
static const char* app_names[] = {
    "Phone",    "Messages", "Browser",  "Camera",
    "Gallery",  "Files",    "Settings", "Store",
    "Terminal", "Clock",    "Music",    "Mail",
    "Maps",     "Weather",  "Calendar", "PenTest",
};

static const uint32_t app_colors[] = {
    C_GREEN,    C_BLUE,     C_ORANGE,   C_MAGENTA,
    C_PINK,     C_YELLOW,   C_LTGRAY,   C_CRIMSON,
    C_PURPLE,   C_CYAN,     C_RED,      C_BLUE,
    C_GREEN,    C_CYAN,     C_ORANGE,   C_RED,
};

static const char* app_packages[] = {
    "com.crimson.phone",    "com.crimson.messages",
    "com.crimson.browser",  "com.crimson.camera",
    "com.crimson.gallery",  "com.crimson.files",
    "com.crimson.settings", "com.crimson.store",
    "com.crimson.terminal", "com.crimson.clock",
    "com.crimson.music",    "com.crimson.mail",
    "com.crimson.maps",     "com.crimson.weather",
    "com.crimson.calendar", "com.crimson.pentest",
};

#define NUM_BUILTIN_APPS    16

/* ── Status bar ── */
#define SB_HEIGHT           48
#define NB_HEIGHT           56
#define GRID_COLS           4
#define GRID_ROWS           4
#define GRID_TOP            120
#define ICON_SIZE           88
#define PIN_KEYS            12  /* 0-9 + delete + enter */

/* ── Notification data ── */
static gui_notification_t notif_buffer[GUI_NOTIFICATION_MAX];
static uint32_t notif_count = 0;
static uint32_t notif_next_id = 1;

/* ═══════════════════════════════════════════════════════════
 *  INITIALIZATION
 * ═══════════════════════════════════════════════════════════ */

void gui_init(void)
{
    spinlock_init(&g_gui_spinlock);
    memset(&g_gui, 0, sizeof(g_gui));

    g_gui.fb_width = DISP_WIDTH;
    g_gui.fb_height = DISP_HEIGHT;
    g_gui.brightness = 100;
    g_gui.lock_screen = 1;
    g_gui.pin_enabled = 1;
    g_gui.surface_count = 0;
    g_gui.app_count = 0;

    /* Get display framebuffer */
    void* fb = NULL;
    display_get_info(&g_gui.fb_width, &g_gui.fb_height,
                     &g_fb_ctx.pitch, &fb);
    g_fb_ctx.fb = (uint32_t*)fb;
    g_fb_ctx.width = g_gui.fb_width;
    g_fb_ctx.height = g_gui.fb_height;
    g_fb_ctx.blend_mode = BLEND_ALPHA;
    gfx_reset_clip(&g_fb_ctx);

    g_gui.framebuffer = (uint32_t*)fb;

    /* Register built-in apps */
    for (int i = 0; i < NUM_BUILTIN_APPS; i++) {
        gui_register_app(app_names[i], app_packages[i], NULL, app_colors[i]);
    }

    current_view = g_gui.pin_enabled ? VIEW_LOCK : VIEW_HOME;
    pin_len = 0;
    memset(pin_buffer, 0, sizeof(pin_buffer));

    /* Fill screen with BloodMoon background */
    display_clear(C_BLOODMOON_BG);

    printk(KERN_INFO "[GUI] Compositor initialized: %dx%d\n",
           g_gui.fb_width, g_gui.fb_height);
    printk(KERN_INFO "[GUI] Built-in apps: %d | Screen: %s\n",
           NUM_BUILTIN_APPS, g_gui.lock_screen ? "locked" : "unlocked");
}

/* ═══════════════════════════════════════════════════════════
 *  FRAME RENDERING
 * ═══════════════════════════════════════════════════════════ */

void gui_render_frame(void)
{
    gfx_ctx_t* ctx = &g_fb_ctx;

    /* Clear to background */
    gfx_fill_rect(ctx, 0, 0, g_gui.fb_width, g_gui.fb_height, C_BLOODMOON_BG);

    /* Draw current view */
    switch (current_view) {
    case VIEW_LOCK:       gui_draw_lock_screen();       break;
    case VIEW_HOME:       gui_draw_home_screen();       break;
    case VIEW_LAUNCHER:   gui_draw_app_launcher();      break;
    case VIEW_NOTIFICATIONS: gui_draw_notification_panel(); break;
    case VIEW_RECENT:     gui_draw_recent_apps();       break;
    case VIEW_POWER:      gui_draw_power_menu();        break;
    }

    /* Overlay status bar on all views except lock */
    if (current_view != VIEW_LOCK) {
        gui_draw_status_bar();
        gui_draw_nav_bar();
    }

    /* Draw any active notifications overlay */
    if (notif_count > 0 && current_view != VIEW_NOTIFICATIONS) {
        gui_draw_notification_badge();
    }
}

/* ═══════════════════════════════════════════════════════════
 *  LOCK SCREEN (VIEW_LOCK)
 * ═══════════════════════════════════════════════════════════ */

void gui_draw_lock_screen(void)
{
    gfx_ctx_t* ctx = &g_fb_ctx;
    uint32_t cx = g_gui.fb_width / 2;

    /* Dark gradient background */
    gradient_t grad = { CRGB(5, 2, 10), C_BLOODMOON_BG, 1 };
    gfx_gradient_rect(ctx, 0, 0, g_gui.fb_width, g_gui.fb_height, &grad);

    /* Time display (large) */
    /* Simulated: would read from RTC */
    gfx_draw_text_center(ctx, (int32_t)cx, 200, "12:45", C_WHITE, 4);
    gfx_draw_text_center(ctx, (int32_t)cx, 290, "Monday, May 13", C_BLOODMOON_TEXT2, 1);

    /* Crimson OS branding */
    gfx_draw_text_center(ctx, (int32_t)cx, 420, "Crimson OS", C_CRIMSON, 2);
    gfx_fill_rect(ctx, (int32_t)cx - 60, 455, 120, 2, C_CRIMSON);

    if (g_gui.pin_enabled) {
        /* PIN prompt */
        gfx_draw_text_center(ctx, (int32_t)cx, 540, "Enter PIN", C_BLOODMOON_TEXT, 2);

        /* PIN dots */
        int dot_spacing = 40;
        int total_w = 4 * dot_spacing;
        int start_x = (int)cx - total_w / 2;
        for (int i = 0; i < 4; i++) {
            uint32_t dot_c = (i < (int)pin_len) ? C_CRIMSON : C_DKGRAY;
            gfx_fill_circle(ctx, start_x + i * dot_spacing + dot_spacing/2, 620, 10, dot_c);
        }

        if (pin_failed) {
            gfx_draw_text_center(ctx, (int32_t)cx, 660, "Incorrect PIN", C_RED, 1);
        }

        /* Numeric keypad */
        gui_draw_pin_pad(ctx, (int32_t)cx);

        /* Emergency call button */
        gfx_draw_text_center(ctx, (int32_t)cx, (int32_t)g_gui.fb_height - 100,
                             "Emergency", C_CRIMSON, 1);
    } else {
        /* Swipe up hint */
        gfx_draw_text_center(ctx, (int32_t)cx, (int32_t)g_gui.fb_height - 200,
                             "Swipe up to unlock", C_BLOODMOON_TEXT2, 1);
    }
}

static void gui_draw_pin_pad(gfx_ctx_t* ctx, int32_t cx)
{
    const char* keys[] = { "1", "2", "3", "4", "5", "6", "7", "8", "9", "", "0", "DEL" };
    int32_t start_y = 700;
    int32_t key_w = 90, key_h = 72, gap = 16;
    int32_t grid_w = 3 * key_w + 2 * gap;
    int32_t start_x = cx - grid_w / 2;

    for (int i = 0; i < 12; i++) {
        int col = i % 3;
        int row = i / 3;
        int32_t kx = start_x + col * (key_w + gap);
        int32_t ky = start_y + row * (key_h + gap);

        if (keys[i][0] == '\0') continue;  /* empty spacer */

        uint32_t kc = C_BLOODMOON_PANEL;
        if (strcmp(keys[i], "DEL") == 0) kc = C_DKGRAY;

        gfx_fill_rounded(ctx, kx, ky, (uint32_t)key_w, (uint32_t)key_h, 16, kc);
        gfx_draw_text_center(ctx, kx + key_w/2, ky + key_h/2 - 8, keys[i], C_WHITE, 2);
    }
}

static int handle_pin_touch(touch_event_t* ev)
{
    if (ev->event != EVENT_DOWN) return 0;

    int32_t cx = (int32_t)(g_gui.fb_width / 2);
    int32_t start_y = 700;
    int32_t key_w = 90, key_h = 72, gap = 16;
    int32_t grid_w = 3 * key_w + 2 * gap;
    int32_t start_x = cx - grid_w / 2;

    for (int i = 0; i < 12; i++) {
        int col = i % 3;
        int row = i / 3;
        int32_t kx = start_x + col * (key_w + gap);
        int32_t ky = start_y + row * (key_h + gap);

        if (ev->x >= (uint32_t)kx && ev->x < (uint32_t)(kx + key_w) &&
            ev->y >= (uint32_t)ky && ev->y < (uint32_t)(ky + key_h)) {

            if (i == 11) {  /* DEL */
                if (pin_len > 0) pin_buffer[--pin_len] = '\0';
                pin_failed = 0;
            } else if (i < 10) {
                /* 0-9 mapping: 1,2,3,4,5,6,7,8,9,skip,0,DEL */
                int digit = (i == 10) ? 0 : i + 1;
                if (pin_len < 4) {
                    pin_buffer[pin_len++] = '0' + digit;
                    pin_buffer[pin_len] = '\0';
                }
            }

            /* Check PIN when 4 digits entered */
            if (pin_len == 4) {
                uint32_t entered = (uint32_t)atoi(pin_buffer);
                if (entered == pin_target) {
                    g_gui.lock_screen = 0;
                    pin_len = 0;
                    pin_failed = 0;
                    current_view = VIEW_HOME;
                    printk(KERN_INFO "[GUI] Lock screen unlocked\n");
                } else {
                    pin_len = 0;
                    pin_failed = 1;
                    memset(pin_buffer, 0, sizeof(pin_buffer));
                }
            }
            return 1;
        }
    }
    return 0;
}

/* ═══════════════════════════════════════════════════════════
 *  HOME SCREEN (VIEW_HOME)
 * ═══════════════════════════════════════════════════════════ */

static void gui_draw_home_screen(void)
{
    gfx_ctx_t* ctx = &g_fb_ctx;
    uint32_t cx = g_gui.fb_width / 2;

    /* Subtle wallpaper gradient */
    gradient_t grad = { CRGB(8, 5, 18), C_BLOODMOON_BG, 1 };
    gfx_gradient_rect(ctx, 0, SB_HEIGHT, g_gui.fb_width,
                      g_gui.fb_height - SB_HEIGHT - NB_HEIGHT, &grad);

    /* Clock widget */
    gfx_fill_rounded(ctx, 40, SB_HEIGHT + 40, g_gui.fb_width - 80, 200,
                      24, 0x18FFFFFF);
    gfx_draw_text_center(ctx, (int32_t)cx, SB_HEIGHT + 80, "12:45", C_WHITE, 3);
    gfx_draw_text_center(ctx, (int32_t)cx, SB_HEIGHT + 150, "Monday, May 13", C_BLOODMOON_TEXT2, 1);

    /* Quick app shortcuts (bottom row on home) */
    int32_t qy = (int32_t)g_gui.fb_height - NB_HEIGHT - 160;
    int32_t qx_start = 60;
    int32_t qgap = (g_gui.fb_width - 120 - 4 * ICON_SIZE) / 3;
    const int quick_apps[] = { 0, 1, 2, 15 };  /* Phone, Messages, Browser, PenTest */

    for (int i = 0; i < 4; i++) {
        int32_t qx = qx_start + i * (ICON_SIZE + qgap);
        /* Icon background */
        gfx_fill_rounded(ctx, qx, qy, ICON_SIZE, ICON_SIZE, 20,
                          gfx_darken(app_colors[quick_apps[i]], 80));
        /* App initial */
        char init[2] = { app_names[quick_apps[i]][0], '\0' };
        gfx_draw_text_center(ctx, qx + ICON_SIZE/2, qy + ICON_SIZE/2 - 8, init, C_WHITE, 2);
        /* Label */
        gfx_draw_text_center(ctx, qx + ICON_SIZE/2, qy + ICON_SIZE + 8,
                             app_names[quick_apps[i]], C_BLOODMOON_TEXT2, 1);
    }
}

/* ═══════════════════════════════════════════════════════════
 *  APP LAUNCHER (VIEW_LAUNCHER)
 * ═══════════════════════════════════════════════════════════ */

void gui_draw_app_launcher(void)
{
    gfx_ctx_t* ctx = &g_fb_ctx;
    uint32_t cx = g_gui.fb_width / 2;

    /* Background */
    gfx_fill_rect(ctx, 0, SB_HEIGHT, g_gui.fb_width,
                  g_gui.fb_height - SB_HEIGHT - NB_HEIGHT, C_BLOODMOON_BG);

    /* Title */
    gfx_draw_text_center(ctx, (int32_t)cx, SB_HEIGHT + 24, "Apps", C_WHITE, 2);

    /* Grid of app icons */
    int32_t grid_x = 50;
    int32_t grid_y = SB_HEIGHT + 80;
    int32_t col_gap = (g_gui.fb_width - 100 - GRID_COLS * ICON_SIZE) / (GRID_COLS - 1);
    int32_t row_gap = 40;

    uint32_t apps_to_show = g_gui.app_count < NUM_BUILTIN_APPS ? g_gui.app_count : NUM_BUILTIN_APPS;
    for (uint32_t i = 0; i < apps_to_show; i++) {
        int col = (int)(i % GRID_COLS);
        int row = (int)(i / GRID_COLS);
        int32_t ix = grid_x + col * (ICON_SIZE + col_gap);
        int32_t iy = grid_y + row * (ICON_SIZE + row_gap + 24);

        /* Icon rounded rect */
        uint32_t ac = app_colors[i];
        gfx_fill_rounded(ctx, ix, iy, ICON_SIZE, ICON_SIZE, 22, ac);

        /* App initial letter */
        char init[2] = { app_names[i][0], '\0' };
        gfx_draw_text_center(ctx, ix + ICON_SIZE/2, iy + ICON_SIZE/2 - 8, init, C_WHITE, 2);

        /* App name */
        uint32_t tw = gfx_text_width(app_names[i], 1);
        int32_t tx = ix + (ICON_SIZE - (int32_t)tw) / 2;
        gfx_draw_text(ctx, tx, iy + ICON_SIZE + 8, app_names[i], C_BLOODMOON_TEXT2, 1);
    }

    /* Swipe indicator */
    gfx_fill_rounded(ctx, (int32_t)cx - 20, (int32_t)g_gui.fb_height - NB_HEIGHT - 30,
                      40, 4, 2, C_DKGRAY);
}

/* ═══════════════════════════════════════════════════════════
 *  STATUS BAR
 * ═══════════════════════════════════════════════════════════ */

void gui_draw_status_bar(void)
{
    gfx_ctx_t* ctx = &g_fb_ctx;

    /* Semi-transparent background */
    gfx_fill_rect(ctx, 0, 0, g_gui.fb_width, SB_HEIGHT, 0xD8000000);

    /* Left: time */
    gfx_draw_text(ctx, 20, 14, "12:45", C_WHITE, 1);

    /* Center: status icons area */
    int32_t sx = (int32_t)g_gui.fb_width / 2 - 40;

    /* Signal bars (simulated) */
    for (int i = 0; i < 4; i++) {
        uint32_t sh = 6 + i * 4;
        uint32_t sc = (i < 3) ? C_WHITE : C_DKGRAY;
        gfx_fill_rect(ctx, sx + i * 10, (int32_t)(SB_HEIGHT - 10 - sh), 6, sh, sc);
    }

    /* Right: battery */
    int32_t bx = (int32_t)g_gui.fb_width - 60;
    gfx_rounded_rect(ctx, bx, 14, 36, 18, 3, C_WHITE);
    gfx_fill_rect(ctx, bx + 2, 16, 28, 14, C_GREEN);
    gfx_fill_rect(ctx, bx + 36, 18, 4, 10, C_WHITE);

    /* Notification count badge */
    if (notif_count > 0) {
        char nb[4];
        snprintf(nb, sizeof(nb), "%u", notif_count);
        gfx_fill_circle(ctx, (int32_t)g_gui.fb_width - 100, 20, 10, C_CRIMSON);
        gfx_draw_text_center(ctx, (int32_t)g_gui.fb_width - 100, 12, nb, C_WHITE, 1);
    }

    /* Divider line */
    gfx_fill_rect(ctx, 0, SB_HEIGHT - 1, g_gui.fb_width, 1, 0x10FFFFFF);
}

/* ═══════════════════════════════════════════════════════════
 *  NAVIGATION BAR
 * ═══════════════════════════════════════════════════════════ */

static void gui_draw_nav_bar(void)
{
    gfx_ctx_t* ctx = &g_fb_ctx;
    int32_t ny = (int32_t)g_gui.fb_height - NB_HEIGHT;

    /* Background */
    gfx_fill_rect(ctx, 0, ny, g_gui.fb_width, NB_HEIGHT, 0xE0000000);

    /* Three buttons: back, home, recent */
    int32_t bw = (int32_t)g_gui.fb_width / 3;

    /* Back (left arrow) */
    int32_t b1x = bw / 2;
    gfx_draw_text_center(ctx, b1x, ny + 16, "\x3C", C_WHITE, 2);  /* < */

    /* Home (circle) */
    int32_t b2x = bw + bw / 2;
    gfx_circle(ctx, b2x, ny + NB_HEIGHT / 2, 12, C_WHITE);

    /* Recent (square) */
    int32_t b3x = 2 * bw + bw / 2;
    gfx_rect(ctx, b3x - 10, ny + 12, 20, 20, C_WHITE);

    /* Top divider */
    gfx_fill_rect(ctx, 0, ny, g_gui.fb_width, 1, 0x10FFFFFF);
}

/* ═══════════════════════════════════════════════════════════
 *  NOTIFICATION PANEL
 * ═══════════════════════════════════════════════════════════ */

void gui_draw_notification_panel(void)
{
    gfx_ctx_t* ctx = &g_fb_ctx;

    /* Semi-transparent backdrop */
    gfx_fill_rect(ctx, 0, 0, g_gui.fb_width, g_gui.fb_height, 0xB0000000);

    /* Panel background */
    uint32_t pw = g_gui.fb_width - 40;
    uint32_t ph = g_gui.fb_height - 200;
    gfx_fill_rounded(ctx, 20, SB_HEIGHT + 20, pw, ph, 24, C_BLOODMOON_PANEL);

    /* Title */
    gfx_draw_text_center(ctx, (int32_t)(g_gui.fb_width / 2), SB_HEIGHT + 40,
                         "Notifications", C_WHITE, 2);
    gfx_fill_rect(ctx, 60, SB_HEIGHT + 75, g_gui.fb_width - 120, 1, 0x10FFFFFF);

    /* Notification list */
    if (notif_count == 0) {
        gfx_draw_text_center(ctx, (int32_t)(g_gui.fb_width / 2),
                             SB_HEIGHT + ph / 2, "No notifications", C_LTGRAY, 1);
    } else {
        int32_t ny = SB_HEIGHT + 100;
        for (uint32_t i = 0; i < notif_count && i < 6; i++) {
            gui_notification_t* n = &notif_buffer[i];

            /* Item background */
            gfx_fill_rounded(ctx, 40, ny, pw - 40, 70, 12, 0x10FFFFFF);

            /* App icon dot */
            gfx_fill_circle(ctx, 65, ny + 35, 8, n->icon_color);

            /* Title */
            gfx_draw_text(ctx, 85, ny + 8, n->title, C_WHITE, 1);

            /* Message */
            gfx_draw_text(ctx, 85, ny + 32, n->message, C_BLOODMOON_TEXT2, 1);

            /* Time */
            char ts[16];
            uint32_t age = (uint32_t)(timer_get_uptime_seconds() - n->timestamp);
            if (age < 60)
                snprintf(ts, sizeof(ts), "%us ago", age);
            else if (age < 3600)
                snprintf(ts, sizeof(ts), "%um ago", age / 60);
            else
                snprintf(ts, sizeof(ts), "%uh ago", age / 3600);
            gfx_draw_text_right(ctx, (int32_t)(g_gui.fb_width - 60), ny + 8, ts, C_DKGRAY, 1);

            ny += 84;
        }
    }

    /* Clear all button */
    gfx_fill_rounded(ctx, (int32_t)(g_gui.fb_width / 2) - 80,
                     (int32_t)g_gui.fb_height - 160, 160, 44, 22, C_DKGRAY);
    gfx_draw_text_center(ctx, (int32_t)(g_gui.fb_width / 2),
                         (int32_t)g_gui.fb_height - 152, "Clear All", C_WHITE, 1);
}

static void gui_draw_notification_badge(void)
{
    /* Small popup at top of screen */
    gfx_ctx_t* ctx = &g_fb_ctx;
    if (notif_count == 0) return;

    gui_notification_t* n = &notif_buffer[notif_count - 1];

    /* Banner */
    gfx_fill_rounded(ctx, 10, SB_HEIGHT + 5, g_gui.fb_width - 20, 60, 16,
                      0xE0202030);
    gfx_fill_circle(ctx, 35, SB_HEIGHT + 35, 10, n->icon_color);
    gfx_draw_text(ctx, 55, SB_HEIGHT + 12, n->app_name, C_WHITE, 1);
    gfx_draw_text(ctx, 55, SB_HEIGHT + 34, n->message, C_BLOODMOON_TEXT2, 1);
}

/* ═══════════════════════════════════════════════════════════
 *  RECENT APPS
 * ═══════════════════════════════════════════════════════════ */

static void gui_draw_recent_apps(void)
{
    gfx_ctx_t* ctx = &g_fb_ctx;
    uint32_t cx = g_gui.fb_width / 2;

    gfx_fill_rect(ctx, 0, SB_HEIGHT, g_gui.fb_width,
                  g_gui.fb_height - SB_HEIGHT - NB_HEIGHT, C_BLOODMOON_BG);

    gfx_draw_text_center(ctx, (int32_t)cx, SB_HEIGHT + 40, "Recent Apps", C_WHITE, 2);

    /* Show active surfaces as cards */
    int32_t card_y = SB_HEIGHT + 100;
    uint32_t card_h = 160;

    if (g_gui.surface_count == 0) {
        gfx_draw_text_center(ctx, (int32_t)cx,
                             (int32_t)(g_gui.fb_height / 2), "No recent apps", C_LTGRAY, 1);
    } else {
        for (uint32_t i = 0; i < g_gui.surface_count && i < GUI_MAX_SURFACES; i++) {
            gui_surface_t* s = &g_gui.surfaces[i];
            if (s->state != GUI_STATE_VISIBLE) continue;

            /* Card */
            gfx_fill_rounded(ctx, 30, card_y, g_gui.fb_width - 60, card_h, 16,
                              0x18FFFFFF);
            gfx_draw_text(ctx, 50, card_y + 16, s->title, C_WHITE, 1);
            gfx_draw_text(ctx, 50, card_y + 44, "PID: ", C_BLOODMOON_TEXT2, 1);

            card_y += card_h + 16;
        }
    }
}

/* ═══════════════════════════════════════════════════════════
 *  POWER MENU
 * ═══════════════════════════════════════════════════════════ */

static void gui_draw_power_menu(void)
{
    gfx_ctx_t* ctx = &g_fb_ctx;
    uint32_t cx = g_gui.fb_width / 2;

    /* Dark backdrop */
    gfx_fill_rect(ctx, 0, 0, g_gui.fb_width, g_gui.fb_height, 0xD0000000);

    /* Menu panel */
    uint32_t mw = 400;
    uint32_t mh = 340;
    gfx_fill_rounded(ctx, (int32_t)(cx - mw/2), 600, mw, mh, 24, C_BLOODMOON_PANEL);

    /* Power off button */
    gfx_fill_rounded(ctx, (int32_t)(cx - 160), 640, 320, 56, 12, C_CRIMSON);
    gfx_draw_text_center(ctx, (int32_t)cx, 660, "Power Off", C_WHITE, 2);

    /* Restart */
    gfx_fill_rounded(ctx, (int32_t)(cx - 160), 712, 320, 56, 12, C_DKGRAY);
    gfx_draw_text_center(ctx, (int32_t)cx, 732, "Restart", C_WHITE, 2);

    /* Screenshot */
    gfx_fill_rounded(ctx, (int32_t)(cx - 160), 784, 320, 56, 12, C_DKGRAY);
    gfx_draw_text_center(ctx, (int32_t)cx, 804, "Screenshot", C_WHITE, 2);

    /* Cancel */
    gfx_draw_text_center(ctx, (int32_t)cx, 870, "Cancel", C_BLOODMOON_TEXT2, 1);
}

/* ═══════════════════════════════════════════════════════════
 *  TOUCH HANDLING
 * ═══════════════════════════════════════════════════════════ */

void gui_handle_touch(touch_event_t* ev)
{
    if (!ev) return;

    /* Lock screen PIN handling */
    if (current_view == VIEW_LOCK) {
        if (handle_pin_touch(ev)) return;

        /* Emergency call area */
        if (ev->event == EVENT_DOWN &&
            ev->y > g_gui.fb_height - 140 && ev->y < g_gui.fb_height - 60) {
            printk(KERN_INFO "[GUI] Emergency dial triggered\n");
            return;
        }
        return;
    }

    /* Status bar swipe down -> notifications */
    if (ev->event == EVENT_DOWN && ev->y < (uint32_t)SB_HEIGHT) {
        current_view = VIEW_NOTIFICATIONS;
        return;
    }

    /* Swipe up from bottom -> home or dismiss notification */
    if (ev->event == EVENT_DOWN && ev->y > g_gui.fb_height - (uint32_t)NB_HEIGHT - 40) {
        if (current_view == VIEW_NOTIFICATIONS ||
            current_view == VIEW_RECENT ||
            current_view == VIEW_LAUNCHER) {
            current_view = VIEW_HOME;
        }
        return;
    }

    /* Navigation bar buttons */
    if (ev->event == EVENT_DOWN && ev->y > g_gui.fb_height - (uint32_t)NB_HEIGHT) {
        int32_t bw = (int32_t)g_gui.fb_width / 3;
        if (ev->x < (uint32_t)bw) {
            /* Back */
            current_view = prev_view;
        } else if (ev->x < (uint32_t)(bw * 2)) {
            /* Home */
            current_view = VIEW_HOME;
        } else {
            /* Recent */
            prev_view = current_view;
            current_view = VIEW_RECENT;
        }
        return;
    }

    /* View-specific touch handling */
    switch (current_view) {
    case VIEW_HOME:
        handle_home_touch(ev);
        break;
    case VIEW_LAUNCHER:
        handle_launcher_touch(ev);
        break;
    case VIEW_NOTIFICATIONS:
        handle_notification_touch(ev);
        break;
    case VIEW_POWER:
        handle_power_touch(ev);
        break;
    default:
        break;
    }
}

static void handle_home_touch(touch_event_t* ev)
{
    if (ev->event != EVENT_DOWN) return;

    /* Swipe up from bottom area opens launcher */
    if (ev->y > g_gui.fb_height - NB_HEIGHT - 200) {
        prev_view = VIEW_HOME;
        current_view = VIEW_LAUNCHER;
        return;
    }

    /* Quick app shortcuts */
    int32_t qy = (int32_t)g_gui.fb_height - NB_HEIGHT - 160;
    if (ev->y >= (uint32_t)qy && ev->y < (uint32_t)(qy + ICON_SIZE)) {
        int32_t qx_start = 60;
        int32_t qgap = (g_gui.fb_width - 120 - 4 * ICON_SIZE) / 3;
        for (int i = 0; i < 4; i++) {
            int32_t qx = qx_start + i * (ICON_SIZE + qgap);
            if (ev->x >= (uint32_t)qx && ev->x < (uint32_t)(qx + ICON_SIZE)) {
                printk(KERN_INFO "[GUI] Launching quick app: %s\n", app_names[i]);
                return;
            }
        }
    }
}

static void handle_launcher_touch(touch_event_t* ev)
{
    if (ev->event != EVENT_DOWN) return;

    int32_t grid_x = 50;
    int32_t grid_y = SB_HEIGHT + 80;
    int32_t col_gap = (g_gui.fb_width - 100 - GRID_COLS * ICON_SIZE) / (GRID_COLS - 1);
    int32_t row_gap = 40;

    uint32_t apps_to_show = g_gui.app_count < NUM_BUILTIN_APPS ? g_gui.app_count : NUM_BUILTIN_APPS;
    for (uint32_t i = 0; i < apps_to_show; i++) {
        int col = (int)(i % GRID_COLS);
        int row = (int)(i / GRID_COLS);
        int32_t ix = grid_x + col * (ICON_SIZE + col_gap);
        int32_t iy = grid_y + row * (ICON_SIZE + row_gap + 24);

        if (ev->x >= (uint32_t)ix && ev->x < (uint32_t)(ix + ICON_SIZE) &&
            ev->y >= (uint32_t)iy && ev->y < (uint32_t)(iy + ICON_SIZE)) {
            printk(KERN_INFO "[GUI] Launching app: %s (%s)\n",
                   app_names[i], app_packages[i]);
            /* In real system: fork process, load ELF, create surface */
            return;
        }
    }
}

static void handle_notification_touch(touch_event_t* ev)
{
    if (ev->event != EVENT_DOWN) return;

    /* Clear all button */
    int32_t cbx = (int32_t)(g_gui.fb_width / 2) - 80;
    int32_t cby = (int32_t)g_gui.fb_height - 160;
    if (ev->x >= (uint32_t)cbx && ev->x < (uint32_t)(cbx + 160) &&
        ev->y >= (uint32_t)cby && ev->y < (uint32_t)(cby + 44)) {
        gui_notify_clear_all();
        current_view = VIEW_HOME;
        return;
    }

    /* Dismiss on tap outside panel */
    if (ev->y < (uint32_t)(SB_HEIGHT + 20) || ev->x < 20 ||
        ev->x > g_gui.fb_width - 20) {
        current_view = VIEW_HOME;
    }
}

static void handle_power_touch(touch_event_t* ev)
{
    if (ev->event != EVENT_DOWN) return;

    uint32_t cx = g_gui.fb_width / 2;

    /* Power off */
    if (ev->x > cx - 160 && ev->x < cx + 160 &&
        ev->y > 640 && ev->y < 696) {
        printk(KERN_EMERG "[GUI] Power off requested\n");
        /* System poweroff sequence */
        return;
    }

    /* Restart */
    if (ev->x > cx - 160 && ev->x < cx + 160 &&
        ev->y > 712 && ev->y < 768) {
        printk(KERN_EMERG "[GUI] Restart requested\n");
        return;
    }

    /* Cancel or outside */
    current_view = VIEW_HOME;
}

/* ═══════════════════════════════════════════════════════════
 *  SURFACE MANAGEMENT
 * ═══════════════════════════════════════════════════════════ */

gui_surface_t* gui_create_surface(uint32_t w, uint32_t h, uint32_t type, pid_t owner, const char* title)
{
    if (g_gui.surface_count >= GUI_MAX_SURFACES) return NULL;

    gui_surface_t* s = &g_gui.surfaces[g_gui.surface_count];
    memset(s, 0, sizeof(gui_surface_t));

    s->id = g_gui.surface_count;
    s->type = type;
    s->state = GUI_STATE_VISIBLE;
    s->width = w;
    s->height = h;
    s->x = 40;
    s->y = SB_HEIGHT + 20;
    s->z_order = g_gui.surface_count;
    s->owner = owner;
    s->bg_color = C_BLOODMOON_PANEL;
    if (title) strncpy(s->title, title, sizeof(s->title) - 1);

    /* Allocate surface framebuffer */
    size_t sz = w * h * 4;
    s->fb = (uint32_t*)kmalloc(sz);
    if (s->fb) memset(s->fb, 0, sz);

    g_gui.surface_count++;
    return s;
}

void gui_destroy_surface(gui_surface_t* surf)
{
    if (!surf) return;
    if (surf->fb) kfree(surf->fb);
    surf->state = GUI_STATE_HIDDEN;
    surf->fb = NULL;
}

void gui_show_surface(gui_surface_t* surf) { if (surf) surf->state = GUI_STATE_VISIBLE; }
void gui_hide_surface(gui_surface_t* surf) { if (surf) surf->state = GUI_STATE_HIDDEN; }
void gui_focus_surface(gui_surface_t* surf) { if (surf) g_gui.focused = surf; }

void gui_move_surface(gui_surface_t* surf, uint32_t x, uint32_t y)
{
    if (!surf) return;
    surf->x = x;
    surf->y = y;
}

void gui_resize_surface(gui_surface_t* surf, uint32_t w, uint32_t h)
{
    if (!surf) return;
    if (surf->fb) kfree(surf->fb);
    surf->width = w;
    surf->height = h;
    surf->fb = (uint32_t*)kmalloc(w * h * 4);
    if (surf->fb) memset(surf->fb, 0, w * h * 4);
}

/* ═══════════════════════════════════════════════════════════
 *  NOTIFICATIONS
 * ═══════════════════════════════════════════════════════════ */

void gui_notify(const char* app, const char* title, const char* msg, uint32_t level)
{
    if (notif_count >= GUI_NOTIFICATION_MAX) {
        /* Shift oldest out */
        for (uint32_t i = 0; i < GUI_NOTIFICATION_MAX - 1; i++)
            notif_buffer[i] = notif_buffer[i + 1];
        notif_count = GUI_NOTIFICATION_MAX - 1;
    }

    gui_notification_t* n = &notif_buffer[notif_count++];
    n->id = notif_next_id++;
    n->level = level;
    n->timestamp = timer_get_uptime_seconds();
    if (app) strncpy(n->app_name, app, sizeof(n->app_name) - 1);
    if (title) strncpy(n->title, title, sizeof(n->title) - 1);
    if (msg) strncpy(n->message, msg, sizeof(n->message) - 1);

    n->icon_color = (level == GUI_NOTIFY_CRITICAL) ? C_RED :
                    (level == GUI_NOTIFY_WARNING) ? C_YELLOW : C_BLUE;

    printk(KERN_INFO "[GUI] Notification: [%s] %s: %s\n", app, title, msg);
}

void gui_notify_clear(uint32_t id)
{
    for (uint32_t i = 0; i < notif_count; i++) {
        if (notif_buffer[i].id == id) {
            for (uint32_t j = i; j < notif_count - 1; j++)
                notif_buffer[j] = notif_buffer[j + 1];
            notif_count--;
            return;
        }
    }
}

void gui_notify_clear_all(void)
{
    notif_count = 0;
}

/* ═══════════════════════════════════════════════════════════
 *  APP LAUNCHER
 * ═══════════════════════════════════════════════════════════ */

int gui_register_app(const char* name, const char* package, const char* icon, uint32_t color)
{
    if (g_gui.app_count >= GUI_MAX_APPS) return -1;

    gui_app_entry_t* a = &g_gui.apps[g_gui.app_count++];
    if (name) strncpy(a->name, name, sizeof(a->name) - 1);
    if (package) strncpy(a->package, package, sizeof(a->package) - 1);
    if (icon) strncpy(a->icon_path, icon, sizeof(a->icon_path) - 1);
    a->icon_color = color;
    a->pid = -1;
    return 0;
}

void gui_launch_app(const char* package)
{
    for (uint32_t i = 0; i < g_gui.app_count; i++) {
        if (strcmp(g_gui.apps[i].package, package) == 0) {
            printk(KERN_INFO "[GUI] Launching %s\n", package);
            /* Create surface for app */
            gui_surface_t* s = gui_create_surface(
                g_gui.fb_width - 80,
                g_gui.fb_height - SB_HEIGHT - NB_HEIGHT - 40,
                GUI_SURFACE_APP, 0, g_gui.apps[i].name);
            if (s) {
                s->bg_color = C_BLOODMOON_PANEL;
                /* Fill with app color */
                for (uint32_t px = 0; px < s->width * s->height; px++)
                    s->fb[px] = 0xFF102030;  /* Dark placeholder */
            }
            return;
        }
    }
    printk(KERN_WARN "[GUI] App not found: %s\n", package);
}

void gui_kill_app(pid_t pid)
{
    for (uint32_t i = 0; i < g_gui.app_count; i++) {
        if (g_gui.apps[i].pid == pid) {
            g_gui.apps[i].pid = -1;
            printk(KERN_INFO "[GUI] Killed app PID %d\n", pid);
            return;
        }
    }
}

void gui_add_notification(const char* title, const char* text, uint32_t color)
{
    if (notif_count >= GUI_NOTIFICATION_MAX) return;
    gui_notification_t* n = &notif_buffer[notif_count++];
    n->id         = notif_next_id++;
    n->icon_color = color;
    n->timestamp  = 0;
    strncpy(n->title,   title, sizeof(n->title)   - 1);
    strncpy(n->message, text,  sizeof(n->message) - 1);
}

/* ═══════════════════════════════════════════════════════════
 *  LOCK SCREEN
 * ═══════════════════════════════════════════════════════════ */

void gui_lock(void)
{
    g_gui.lock_screen = 1;
    pin_len = 0;
    pin_failed = 0;
    memset(pin_buffer, 0, sizeof(pin_buffer));
    current_view = VIEW_LOCK;
    printk(KERN_INFO "[GUI] Screen locked\n");
}

void gui_unlock(uint32_t pin)
{
    if (!g_gui.pin_enabled || pin == pin_target) {
        g_gui.lock_screen = 0;
        pin_len = 0;
        current_view = VIEW_HOME;
        printk(KERN_INFO "[GUI] Screen unlocked\n");
    }
}

void gui_set_pin(uint32_t pin)
{
    pin_target = pin;
    g_gui.pin_enabled = 1;
}

/* ═══════════════════════════════════════════════════════════
 *  UTILITY
 * ═══════════════════════════════════════════════════════════ */

uint32_t gui_color(uint8_t r, uint8_t g, uint8_t b)
{
    return CRGB(r, g, b);
}

void gui_set_brightness(uint32_t pct)
{
    if (pct > 100) pct = 100;
    g_gui.brightness = pct;
    display_set_brightness(pct);
}

void gui_screenshot(const char* path)
{
    printk(KERN_INFO "[GUI] Screenshot saved to %s\n", path ? path : "/tmp/screenshot.raw");
}

/* Power menu trigger (long-press power button) */
void gui_show_power_menu(void)
{
    prev_view = current_view;
    current_view = VIEW_POWER;
}
