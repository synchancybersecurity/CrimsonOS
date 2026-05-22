/*
 * Crimson OS - GUI Subsystem Header
 * Display Compositor & Window Manager
 */

#ifndef _CRIMSON_GUI_H
#define _CRIMSON_GUI_H

#include <crimson/types.h>
#include <crimson/display.h>
#include <crimson/touch.h>

#define GUI_MAX_SURFACES        32
#define GUI_MAX_LAYERS          8
#define GUI_FB_WIDTH            1080
#define GUI_FB_HEIGHT           1920
#define GUI_STATUS_BAR_HEIGHT   48
#define GUI_NAV_BAR_HEIGHT      56
#define GUI_LAUNCHER_COLS       4
#define GUI_LAUNCHER_ROWS       4
#define GUI_MAX_APPS            64
#define GUI_ICON_SIZE           88
#define GUI_NOTIFICATION_MAX    16

/* Surface types */
#define GUI_SURFACE_APP         1
#define GUI_SURFACE_DIALOG      2
#define GUI_SURFACE_NOTIFICATION 3
#define GUI_SURFACE_OVERLAY     4
#define GUI_SURFACE_WALLPAPER   5

/* Surface states */
#define GUI_STATE_VISIBLE       1
#define GUI_STATE_HIDDEN        2
#define GUI_STATE_MINIMIZED     3

/* Notification levels */
#define GUI_NOTIFY_INFO         1
#define GUI_NOTIFY_WARNING      2
#define GUI_NOTIFY_CRITICAL     3

typedef struct gui_surface {
    uint32_t        id;
    uint32_t        type;
    uint32_t        state;
    uint32_t        x, y;
    uint32_t        width, height;
    uint32_t        z_order;
    uint32_t*       fb;
    pid_t           owner;
    char            title[64];
    uint32_t        bg_color;
    uint32_t        flags;
    struct gui_surface* next;
} gui_surface_t;

typedef struct gui_notification {
    uint32_t    id;
    uint32_t    level;
    char        app_name[32];
    char        title[128];
    char        message[256];
    uint64_t    timestamp;
    uint32_t    icon_color;
} gui_notification_t;

typedef struct gui_app_entry {
    char        name[64];
    char        package[128];
    char        icon_path[128];
    uint32_t    icon_color;
    pid_t       pid;
    uint32_t    flags;
} gui_app_entry_t;

typedef struct gui_state {
    uint32_t            fb_width;
    uint32_t            fb_height;
    uint32_t*           framebuffer;
    gui_surface_t       surfaces[GUI_MAX_SURFACES];
    uint32_t            surface_count;
    gui_surface_t*      wallpaper;
    gui_surface_t*      status_bar;
    gui_surface_t*      nav_bar;
    gui_surface_t*      focused;
    gui_notification_t  notifications[GUI_NOTIFICATION_MAX];
    uint32_t            notification_count;
    gui_app_entry_t     apps[GUI_MAX_APPS];
    uint32_t            app_count;
    uint32_t            lock_screen;
    uint32_t            pin_code;
    uint32_t            pin_enabled;
    uint32_t            brightness;
    uint32_t            power_menu_visible;
    uint64_t            last_touch_time;
} gui_state_t;

/* ── Core compositor ── */
void gui_init(void);
void gui_render_frame(void);
void gui_handle_touch(touch_event_t* ev);

/* ── Surface management ── */
gui_surface_t* gui_create_surface(uint32_t w, uint32_t h, uint32_t type, pid_t owner, const char* title);
void gui_destroy_surface(gui_surface_t* surf);
void gui_show_surface(gui_surface_t* surf);
void gui_hide_surface(gui_surface_t* surf);
void gui_focus_surface(gui_surface_t* surf);
void gui_move_surface(gui_surface_t* surf, uint32_t x, uint32_t y);
void gui_resize_surface(gui_surface_t* surf, uint32_t w, uint32_t h);

/* ── View drawing ── */
void gui_draw_status_bar(void);
void gui_draw_app_launcher(void);
void gui_draw_lock_screen(void);
void gui_draw_notification_panel(void);

/* ── Touch handlers ── */

/* ── Notifications ── */
void gui_notify(const char* app, const char* title, const char* msg, uint32_t level);
void gui_notify_clear(uint32_t id);
void gui_notify_clear_all(void);

/* ── App launcher ── */
int gui_register_app(const char* name, const char* package, const char* icon, uint32_t color);
void gui_launch_app(const char* package);
void gui_kill_app(pid_t pid);

/* ── Lock screen ── */
void gui_unlock(uint32_t pin);
void gui_set_pin(uint32_t pin);

/* ── Power menu ── */
void gui_show_power_menu(void);

/* ── Screen capture ── */
void gui_screenshot(const char* path);

/* ── Utility ── */
uint32_t gui_color(uint8_t r, uint8_t g, uint8_t b);
void gui_set_brightness(uint32_t pct);

/* ── Widget system ── */
typedef struct button_widget button_widget_t;
typedef struct label_widget label_widget_t;
typedef struct textinput_widget textinput_widget_t;
typedef struct switch_widget switch_widget_t;
typedef struct slider_widget slider_widget_t;
typedef struct list_widget list_widget_t;
typedef struct progress_widget progress_widget_t;

button_widget_t*    gui_button(int32_t x, int32_t y, uint32_t w, uint32_t h, const char* text);
button_widget_t*    gui_icon_button(int32_t x, int32_t y, uint32_t size, const char* label);
label_widget_t*     gui_label(int32_t x, int32_t y, const char* text);
textinput_widget_t* gui_text_input(int32_t x, int32_t y, uint32_t w, const char* placeholder);
switch_widget_t*    gui_switch(int32_t x, int32_t y, uint32_t on);
slider_widget_t*    gui_slider(int32_t x, int32_t y, uint32_t w, uint32_t min, uint32_t max, uint32_t val);
list_widget_t*      gui_list(int32_t x, int32_t y, uint32_t w, uint32_t h);
progress_widget_t*  gui_progress(int32_t x, int32_t y, uint32_t w, uint32_t initial);

void gui_draw_widget(gfx_ctx_t* ctx, void* widget);
void gui_draw_all(gfx_ctx_t* ctx);
int gui_handle_touch_widget(void* widget, touch_event_t* ev);
void gui_handle_touch_all(touch_event_t* ev);

void gui_button_set_handler(button_widget_t* btn, void (*handler)(void*), void* data);
void gui_button_set_style(button_widget_t* btn, uint32_t style);
void gui_label_set_align(label_widget_t* lbl, uint32_t align);
void gui_label_set_scale(label_widget_t* lbl, uint32_t scale);
void gui_input_set_text(textinput_widget_t* inp, const char* text);
const char* gui_input_get_text(textinput_widget_t* inp);
void gui_list_add(list_widget_t* lst, const char* text, const char* sub, uint32_t color);
void gui_list_clear(list_widget_t* lst);
void gui_progress_set(progress_widget_t* pr, uint32_t pct);
void gui_progress_set_indeterminate(progress_widget_t* pr);
void gui_widget_set_visible(void* widget, uint32_t visible);
void gui_widget_set_enabled(void* widget, uint32_t enabled);
void gui_widget_move(void* widget, int32_t x, int32_t y);
void gui_widget_set_size(void* widget, uint32_t w, uint32_t h);
void gui_widget_set_color(void* widget, uint32_t bg, uint32_t fg);
void gui_widget_destroy(void* widget);
void gui_widgets_clear(void);
uint32_t gui_widget_count(void);

/* ── Global GUI state ── */
extern gui_state_t g_gui;

#endif /* _CRIMSON_GUI_H */
