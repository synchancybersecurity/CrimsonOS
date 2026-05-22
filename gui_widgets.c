/*
 * Crimson OS - GUI Widget System
 * Buttons, labels, text input, scroll views, lists, switches, sliders
 */

#include <crimson/types.h>
#include <crimson/display.h>
#include <crimson/gui.h>
#include <crimson/string.h>
#include <crimson/memory.h>
#include <crimson/touch.h>
#include <crimson/mm.h>

/* ── Widget types ── */
#define WIDGET_BUTTON           1
#define WIDGET_LABEL            2
#define WIDGET_TEXT_INPUT       3
#define WIDGET_SWITCH           4
#define WIDGET_SLIDER           5
#define WIDGET_LIST             6
#define WIDGET_ICON_BUTTON      7
#define WIDGET_CARD             8
#define WIDGET_SEPARATOR        9
#define WIDGET_PROGRESS         10
#define WIDGET_IMAGE            11

/* ── Widget states ── */
#define WSTATE_IDLE             0
#define WSTATE_HOVER            1
#define WSTATE_PRESSED          2
#define WSTATE_DISABLED         3
#define WSTATE_FOCUSED          4

/* ── Animation states ── */
#define ANIM_NONE               0
#define ANIM_PRESS              1
#define ANIM_RELEASE            2
#define ANIM_FADE_IN            3
#define ANIM_SLIDE_UP           4
#define ANIM_SLIDE_DOWN         5

typedef struct {
    uint32_t    type;
    uint32_t    state;
    int32_t     x, y;
    uint32_t    w, h;
    uint32_t    bg_color;
    uint32_t    fg_color;
    uint32_t    border_color;
    uint32_t    radius;
    uint32_t    visible;
    uint32_t    enabled;
    uint32_t    tag;
    void*       user_data;
    /* Animation */
    uint32_t    anim_type;
    uint32_t    anim_start;
    uint32_t    anim_duration;
    float       anim_progress;
} widget_t;

/* Font metrics (matches gui_graphics.c) */
#ifndef FONT_W
#define FONT_W  8
#define FONT_H  16
#endif

/* Named struct definitions satisfying the forward declarations in gui.h */
struct button_widget {
    widget_t    base;
    char        text[64];
    uint32_t    icon;
    uint32_t    style;
    void        (*on_click)(void*);
    void*       click_data;
};
typedef struct button_widget button_widget_t;

struct label_widget {
    widget_t    base;
    char        text[128];
    uint32_t    align;
    uint32_t    font_scale;
};
typedef struct label_widget label_widget_t;

struct textinput_widget {
    widget_t    base;
    char        text[256];
    char        placeholder[64];
    uint32_t    max_len;
    uint32_t    cursor_pos;
    uint32_t    show_cursor;
    uint32_t    cursor_blink_time;
    uint32_t    password;
    uint32_t    focused;
    void        (*on_change)(const char*);
};
typedef struct textinput_widget textinput_widget_t;

struct switch_widget {
    widget_t    base;
    uint32_t    on;
    uint32_t    anim_x;
    void        (*on_toggle)(uint32_t state);
};
typedef struct switch_widget switch_widget_t;

struct slider_widget {
    widget_t    base;
    uint32_t    min, max, value;
    uint32_t    dragging;
    void        (*on_change)(uint32_t val);
};
typedef struct slider_widget slider_widget_t;

typedef struct {
    char        text[128];
    char        sub[128];
    uint32_t    icon_color;
    uint32_t    selected;
    void        (*on_click)(void);
} list_item_t;

struct list_widget {
    widget_t    base;
    list_item_t items[64];
    uint32_t    item_count;
    uint32_t    scroll_y;
    uint32_t    selected_idx;
    uint32_t    item_height;
    void        (*on_select)(uint32_t idx);
};
typedef struct list_widget list_widget_t;

struct progress_widget {
    widget_t    base;
    uint32_t    progress;
    uint32_t    indeterminate;
    uint32_t    anim_offset;
};
typedef struct progress_widget progress_widget_t;

/* ── Internal widget pool ── */
#define MAX_WIDGETS     256
static widget_t* widget_pool[MAX_WIDGETS];
static uint32_t widget_count = 0;

/* ═══════════════════════════════════════════════════════════
 *  WIDGET CREATION
 * ═══════════════════════════════════════════════════════════ */

static widget_t* widget_alloc(uint32_t type, int32_t x, int32_t y, uint32_t w, uint32_t h)
{
    if (widget_count >= MAX_WIDGETS) return NULL;
    widget_t* wgt = kmalloc(sizeof(button_widget_t));
    if (!wgt) return NULL;
    memset(wgt, 0, sizeof(button_widget_t));
    wgt->type = type;
    wgt->x = x;
    wgt->y = y;
    wgt->w = w;
    wgt->h = h;
    wgt->visible = 1;
    wgt->enabled = 1;
    wgt->state = WSTATE_IDLE;
    wgt->bg_color = C_BLOODMOON_PANEL;
    wgt->fg_color = C_BLOODMOON_TEXT;
    wgt->radius = 8;
    widget_pool[widget_count++] = wgt;
    return wgt;
}

button_widget_t* gui_button(int32_t x, int32_t y, uint32_t w, uint32_t h, const char* text)
{
    button_widget_t* btn = (button_widget_t*)widget_alloc(WIDGET_BUTTON, x, y, w, h);
    if (!btn) return NULL;
    btn->base.bg_color = C_CRIMSON;
    btn->base.fg_color = C_WHITE;
    btn->base.radius = 12;
    if (text) strncpy(btn->text, text, sizeof(btn->text) - 1);
    return btn;
}

button_widget_t* gui_icon_button(int32_t x, int32_t y, uint32_t size, const char* label)
{
    button_widget_t* btn = (button_widget_t*)widget_alloc(WIDGET_ICON_BUTTON, x, y, size, size + 20);
    if (!btn) return NULL;
    btn->base.bg_color = 0;
    btn->base.fg_color = C_BLOODMOON_TEXT;
    btn->base.radius = size / 4;
    if (label) strncpy(btn->text, label, sizeof(btn->text) - 1);
    return btn;
}

label_widget_t* gui_label(int32_t x, int32_t y, const char* text)
{
    label_widget_t* lbl = (label_widget_t*)widget_alloc(WIDGET_LABEL, x, y,
                            text ? (uint32_t)strlen(text) * 8 + 8 : 8, 20);
    if (!lbl) return NULL;
    lbl->base.bg_color = 0;  /* Transparent */
    lbl->font_scale = 1;
    if (text) {
        strncpy(lbl->text, text, sizeof(lbl->text) - 1);
        lbl->base.w = (uint32_t)strlen(text) * 8 + 8;
    }
    return lbl;
}

textinput_widget_t* gui_text_input(int32_t x, int32_t y, uint32_t w, const char* placeholder)
{
    textinput_widget_t* inp = (textinput_widget_t*)widget_alloc(WIDGET_TEXT_INPUT, x, y, w, 48);
    if (!inp) return NULL;
    inp->base.bg_color = C_BLOODMOON_PANEL;
    inp->base.fg_color = C_BLOODMOON_TEXT;
    inp->base.border_color = C_DKGRAY;
    inp->base.radius = 8;
    inp->max_len = sizeof(inp->text) - 1;
    inp->show_cursor = 1;
    if (placeholder) strncpy(inp->placeholder, placeholder, sizeof(inp->placeholder) - 1);
    return inp;
}

switch_widget_t* gui_switch(int32_t x, int32_t y, uint32_t on)
{
    switch_widget_t* sw = (switch_widget_t*)widget_alloc(WIDGET_SWITCH, x, y, 56, 32);
    if (!sw) return NULL;
    sw->on = on;
    sw->anim_x = on ? 26 : 2;
    return sw;
}

slider_widget_t* gui_slider(int32_t x, int32_t y, uint32_t w, uint32_t min, uint32_t max, uint32_t val)
{
    slider_widget_t* sl = (slider_widget_t*)widget_alloc(WIDGET_SLIDER, x, y, w, 24);
    if (!sl) return NULL;
    sl->min = min;
    sl->max = max;
    sl->value = val;
    sl->base.fg_color = C_CRIMSON;
    sl->base.bg_color = C_DKGRAY;
    return sl;
}

list_widget_t* gui_list(int32_t x, int32_t y, uint32_t w, uint32_t h)
{
    list_widget_t* lst = (list_widget_t*)widget_alloc(WIDGET_LIST, x, y, w, h);
    if (!lst) return NULL;
    lst->item_height = 56;
    lst->base.bg_color = 0;
    return lst;
}

progress_widget_t* gui_progress(int32_t x, int32_t y, uint32_t w, uint32_t initial)
{
    progress_widget_t* pr = (progress_widget_t*)widget_alloc(WIDGET_PROGRESS, x, y, w, 8);
    if (!pr) return NULL;
    pr->progress = initial;
    pr->base.bg_color = C_DKGRAY;
    pr->base.fg_color = C_CRIMSON;
    pr->base.radius = 4;
    return pr;
}

/* ═══════════════════════════════════════════════════════════
 *  WIDGET DRAWING
 * ═══════════════════════════════════════════════════════════ */

static void draw_button(gfx_ctx_t* ctx, button_widget_t* btn)
{
    if (!btn->base.visible) return;
    widget_t* b = &btn->base;

    uint32_t bg = b->bg_color;
    uint32_t fg = b->fg_color;

    if (!b->enabled) {
        bg = gfx_darken(bg, 100);
        fg = gfx_darken(fg, 100);
    } else if (b->state == WSTATE_PRESSED) {
        bg = gfx_darken(bg, 40);
    } else if (b->state == WSTATE_HOVER) {
        bg = gfx_lighten(bg, 20);
    }

    /* Shadow for filled buttons */
    if (btn->style == 0 && b->enabled) {
        gfx_fill_rounded(ctx, b->x + 2, b->y + 3, b->w, b->h, b->radius,
                          0x40000000);
    }

    gfx_fill_rounded(ctx, b->x, b->y, b->w, b->h, b->radius, bg);

    if (btn->style == 1) {
        gfx_rounded_rect(ctx, b->x, b->y, b->w, b->h, b->radius, bg);
    }

    /* Text */
    if (btn->text[0]) {
        uint32_t tw = gfx_text_width(btn->text, 1);
        uint32_t tx = b->x + (b->w - tw) / 2;
        uint32_t ty = b->y + (b->h - FONT_H) / 2;
        gfx_draw_text(ctx, (int32_t)tx, (int32_t)ty, btn->text, fg, 1);
    }
}

static void draw_icon_button(gfx_ctx_t* ctx, button_widget_t* btn)
{
    if (!btn->base.visible) return;
    widget_t* b = &btn->base;

    uint32_t bg = b->bg_color;
    if (b->state == WSTATE_PRESSED)
        bg = gfx_darken(C_BLOODMOON_PANEL, 40);
    else if (b->state == WSTATE_HOVER)
        bg = C_BLOODMOON_PANEL;

    uint32_t icon_size = b->w;
    if (bg) {
        gfx_fill_rounded(ctx, b->x, b->y, icon_size, icon_size,
                          b->radius, bg);
    }

    /* Draw icon (placeholder: colored circle with letter) */
    uint32_t cx = b->x + icon_size / 2;
    uint32_t cy = b->y + icon_size / 2;
    gfx_fill_circle(ctx, (int32_t)cx, (int32_t)cy, icon_size / 3, C_CRIMSON);

    /* Label below */
    if (btn->text[0]) {
        uint32_t tw = gfx_text_width(btn->text, 1);
        gfx_draw_text(ctx, (int32_t)(b->x + (b->w - tw) / 2),
                      (int32_t)(b->y + icon_size + 4),
                      btn->text, C_BLOODMOON_TEXT2, 1);
    }
}

static void draw_label(gfx_ctx_t* ctx, label_widget_t* lbl)
{
    if (!lbl->base.visible) return;
    widget_t* b = &lbl->base;

    int32_t tx = b->x;
    if (lbl->align == 1) {
        uint32_t tw = gfx_text_width(lbl->text, lbl->font_scale);
        tx = b->x + (int32_t)(b->w - tw) / 2;
    } else if (lbl->align == 2) {
        uint32_t tw = gfx_text_width(lbl->text, lbl->font_scale);
        tx = b->x + (int32_t)(b->w - tw);
    }

    gfx_draw_text(ctx, tx, b->y, lbl->text, b->fg_color, lbl->font_scale);
}

static void draw_text_input(gfx_ctx_t* ctx, textinput_widget_t* inp)
{
    if (!inp->base.visible) return;
    widget_t* b = &inp->base;

    /* Background */
    uint32_t border = b->border_color;
    if (inp->focused) border = C_CRIMSON;

    gfx_fill_rounded(ctx, b->x, b->y, b->w, b->h, b->radius, b->bg_color);
    gfx_rounded_rect(ctx, b->x, b->y, b->w, b->h, b->radius, border);

    /* Text or placeholder */
    int32_t tx = b->x + 12;
    int32_t ty = b->y + (int32_t)(b->h - FONT_H) / 2;

    if (inp->text[0]) {
        if (inp->password) {
            char dots[256];
            uint32_t len = (uint32_t)strlen(inp->text);
            if (len > 255) len = 255;
            for (uint32_t i = 0; i < len; i++) dots[i] = '*';
            dots[len] = '\0';
            gfx_draw_text(ctx, tx, ty, dots, b->fg_color, 1);
        } else {
            gfx_draw_text(ctx, tx, ty, inp->text, b->fg_color, 1);
        }

        /* Cursor */
        if (inp->focused && inp->show_cursor) {
            uint32_t cw = inp->password ? inp->cursor_pos * 8 : gfx_text_width(inp->text, 1);
            if (cw > b->w - 24) cw = b->w - 24;
            gfx_fill_rect(ctx, tx + (int32_t)cw, ty - 2, 2, FONT_H + 4, C_CRIMSON);
        }
    } else if (inp->placeholder[0]) {
        gfx_draw_text(ctx, tx, ty, inp->placeholder, C_LTGRAY, 1);
    }
}

static void draw_switch(gfx_ctx_t* ctx, switch_widget_t* sw)
{
    if (!sw->base.visible) return;
    widget_t* b = &sw->base;

    /* Track */
    uint32_t track_color = sw->on ? gfx_darken(b->fg_color, 60) : C_DKGRAY;
    gfx_fill_rounded(ctx, b->x, b->y + 4, b->w, b->h - 8, (b->h - 8) / 2, track_color);

    /* Thumb */
    uint32_t thumb_x = sw->on ? b->x + b->w - b->h : b->x;
    /* Animated position */
    uint32_t tx = b->x + sw->anim_x;
    gfx_fill_circle(ctx, (int32_t)(tx + (b->h - 4) / 2), (int32_t)(b->y + (int32_t)b->h / 2),
                     (b->h - 4) / 2, C_WHITE);
}

static void draw_slider(gfx_ctx_t* ctx, slider_widget_t* sl)
{
    if (!sl->base.visible) return;
    widget_t* b = &sl->base;

    uint32_t cy = b->y + b->h / 2;

    /* Track background */
    gfx_fill_rounded(ctx, b->x, (int32_t)cy - 3, b->w, 6, 3, b->bg_color);

    /* Filled portion */
    uint32_t range = sl->max - sl->min;
    if (range == 0) range = 1;
    uint32_t fill_w = ((sl->value - sl->min) * b->w) / range;
    gfx_fill_rounded(ctx, b->x, (int32_t)cy - 3, fill_w, 6, 3, b->fg_color);

    /* Thumb */
    gfx_fill_circle(ctx, (int32_t)(b->x + fill_w), (int32_t)cy, 10, C_WHITE);
    gfx_circle(ctx, (int32_t)(b->x + fill_w), (int32_t)cy, 10, C_LTGRAY);
}

static void draw_list(gfx_ctx_t* ctx, list_widget_t* lst)
{
    if (!lst->base.visible) return;
    widget_t* b = &lst->base;

    gfx_set_clip(ctx, b->x, b->y, b->w, b->h);

    for (uint32_t i = 0; i < lst->item_count; i++) {
        list_item_t* item = &lst->items[i];
        int32_t iy = b->y + (int32_t)(i * lst->item_height) - (int32_t)lst->scroll_y;

        if (iy + (int32_t)lst->item_height < b->y || iy > (int32_t)(b->y + b->h))
            continue;

        /* Selection background */
        if (i == lst->selected_idx) {
            gfx_fill_rect(ctx, b->x, iy, b->w, lst->item_height, 0x20FFFFFF);
        }

        /* Icon dot */
        gfx_fill_circle(ctx, b->x + 20, iy + (int32_t)(lst->item_height / 2), 6, item->icon_color);

        /* Text */
        gfx_draw_text(ctx, b->x + 40, iy + 8, item->text, C_BLOODMOON_TEXT, 1);
        gfx_draw_text(ctx, b->x + 40, iy + 28, item->sub, C_BLOODMOON_TEXT2, 1);

        /* Separator */
        gfx_fill_rect(ctx, b->x + 40, iy + (int32_t)lst->item_height - 1, b->w - 40, 1, 0x10FFFFFF);
    }

    gfx_reset_clip(ctx);
}

static void draw_progress(gfx_ctx_t* ctx, progress_widget_t* pr)
{
    if (!pr->base.visible) return;
    widget_t* b = &pr->base;

    /* Background track */
    gfx_fill_rounded(ctx, b->x, b->y, b->w, b->h, b->radius, b->bg_color);

    if (pr->indeterminate) {
        /* Animated bar */
        uint32_t bar_w = b->w / 4;
        uint32_t offset = (pr->anim_offset * b->w) / 256;
        gfx_fill_rounded(ctx, b->x + (int32_t)offset, b->y, bar_w, b->h, b->radius, b->fg_color);
    } else {
        /* Filled portion */
        uint32_t fill_w = (pr->progress * b->w) / 100;
        if (fill_w > 2) {
            gfx_fill_rounded(ctx, b->x, b->y, fill_w, b->h, b->radius, b->fg_color);
        }
    }
}

static void draw_card(gfx_ctx_t* ctx, widget_t* wgt)
{
    if (!wgt->visible) return;

    /* Shadow */
    gfx_fill_rounded(ctx, wgt->x + 2, wgt->y + 3, wgt->w, wgt->h, wgt->radius,
                      0x30000000);
    /* Card body */
    gfx_fill_rounded(ctx, wgt->x, wgt->y, wgt->w, wgt->h, wgt->radius,
                      wgt->bg_color);
    /* Subtle border */
    gfx_rounded_rect(ctx, wgt->x, wgt->y, wgt->w, wgt->h, wgt->radius,
                      0x08FFFFFF);
}

static void draw_separator(gfx_ctx_t* ctx, widget_t* wgt)
{
    if (!wgt->visible) return;
    gfx_fill_rect(ctx, wgt->x, wgt->y, wgt->w, 1, 0x10FFFFFF);
}

/* ═══════════════════════════════════════════════════════════
 *  PUBLIC DRAW / EVENT API
 * ═══════════════════════════════════════════════════════════ */

void gui_draw_widget(gfx_ctx_t* ctx, void* widget)
{
    if (!ctx || !widget) return;
    widget_t* w = (widget_t*)widget;

    switch (w->type) {
    case WIDGET_BUTTON:       draw_button(ctx, (button_widget_t*)w); break;
    case WIDGET_ICON_BUTTON:  draw_icon_button(ctx, (button_widget_t*)w); break;
    case WIDGET_LABEL:        draw_label(ctx, (label_widget_t*)w); break;
    case WIDGET_TEXT_INPUT:   draw_text_input(ctx, (textinput_widget_t*)w); break;
    case WIDGET_SWITCH:       draw_switch(ctx, (switch_widget_t*)w); break;
    case WIDGET_SLIDER:       draw_slider(ctx, (slider_widget_t*)w); break;
    case WIDGET_LIST:         draw_list(ctx, (list_widget_t*)w); break;
    case WIDGET_PROGRESS:     draw_progress(ctx, (progress_widget_t*)w); break;
    case WIDGET_CARD:         draw_card(ctx, w); break;
    case WIDGET_SEPARATOR:    draw_separator(ctx, w); break;
    default: break;
    }
}

void gui_draw_all(gfx_ctx_t* ctx)
{
    if (!ctx) return;
    for (uint32_t i = 0; i < widget_count; i++) {
        gui_draw_widget(ctx, widget_pool[i]);
    }
}

/* ── Touch event handling ── */
int gui_handle_touch_widget(void* widget, touch_event_t* ev)
{
    if (!widget || !ev) return 0;
    widget_t* w = (widget_t*)widget;
    if (!w->visible || !w->enabled) return 0;

    /* Check if touch is inside widget */
    int inside = (ev->x >= (uint32_t)w->x && ev->x < (uint32_t)(w->x + (int32_t)w->w) &&
                  ev->y >= (uint32_t)w->y && ev->y < (uint32_t)(w->y + (int32_t)w->h));

    if (w->type == WIDGET_BUTTON || w->type == WIDGET_ICON_BUTTON) {
        button_widget_t* btn = (button_widget_t*)w;
        if (ev->event == EVENT_DOWN && inside) {
            w->state = WSTATE_PRESSED;
            return 1;
        }
        if (ev->event == EVENT_UP && w->state == WSTATE_PRESSED) {
            w->state = WSTATE_IDLE;
            if (inside && btn->on_click)
                btn->on_click(btn->click_data);
            return 1;
        }
        if (ev->event == EVENT_CONTACT && !inside && w->state == WSTATE_PRESSED) {
            w->state = WSTATE_IDLE;
        }
    }

    if (w->type == WIDGET_SWITCH) {
        switch_widget_t* sw = (switch_widget_t*)w;
        if (ev->event == EVENT_DOWN && inside) {
            sw->on = !sw->on;
            sw->anim_x = sw->on ? 26 : 2;
            if (sw->on_toggle) sw->on_toggle(sw->on);
            return 1;
        }
    }

    if (w->type == WIDGET_TEXT_INPUT) {
        textinput_widget_t* inp = (textinput_widget_t*)w;
        if (ev->event == EVENT_DOWN) {
            inp->focused = inside;
            return inside ? 1 : 0;
        }
    }

    if (w->type == WIDGET_SLIDER) {
        slider_widget_t* sl = (slider_widget_t*)w;
        if (ev->event == EVENT_DOWN && inside) {
            sl->dragging = 1;
            /* Set value from touch position */
            uint32_t rel_x = (ev->x > (uint32_t)w->x) ? (ev->x - (uint32_t)w->x) : 0;
            uint32_t range = sl->max - sl->min;
            sl->value = sl->min + (rel_x * range) / w->w;
            if (sl->value > sl->max) sl->value = sl->max;
            return 1;
        }
        if (ev->event == EVENT_UP && sl->dragging) {
            sl->dragging = 0;
            return 1;
        }
        if (ev->event == EVENT_CONTACT && sl->dragging) {
            uint32_t rel_x = (ev->x > (uint32_t)w->x) ? (ev->x - (uint32_t)w->x) : 0;
            uint32_t range = sl->max - sl->min;
            sl->value = sl->min + (rel_x * range) / w->w;
            if (sl->value > sl->max) sl->value = sl->max;
            if (sl->on_change) sl->on_change(sl->value);
            return 1;
        }
    }

    if (w->type == WIDGET_LIST) {
        list_widget_t* lst = (list_widget_t*)w;
        if (ev->event == EVENT_DOWN && inside) {
            uint32_t rel_y = ev->y - (uint32_t)w->y + lst->scroll_y;
            uint32_t idx = rel_y / lst->item_height;
            if (idx < lst->item_count) {
                lst->selected_idx = idx;
                if (lst->on_select) lst->on_select(idx);
                if (lst->items[idx].on_click) lst->items[idx].on_click();
            }
            return 1;
        }
    }

    return inside ? 1 : 0;
}

void gui_handle_touch_all(touch_event_t* ev)
{
    if (!ev) return;
    /* Process in reverse z-order (topmost first) */
    for (int i = (int)widget_count - 1; i >= 0; i--) {
        if (gui_handle_touch_widget(widget_pool[i], ev)) break;
    }
}

/* ═══════════════════════════════════════════════════════════
 *  WIDGET SETUP
 * ═══════════════════════════════════════════════════════════ */

void gui_button_set_handler(button_widget_t* btn, void (*handler)(void*), void* data)
{
    if (!btn) return;
    btn->on_click = handler;
    btn->click_data = data;
}

void gui_button_set_style(button_widget_t* btn, uint32_t style)
{
    if (!btn) return;
    btn->style = style;
    if (style == 2) {  /* Text-only */
        btn->base.bg_color = 0;
        btn->base.fg_color = C_CRIMSON;
    }
}

void gui_label_set_align(label_widget_t* lbl, uint32_t align)
{
    if (!lbl) return;
    lbl->align = align;
}

void gui_label_set_scale(label_widget_t* lbl, uint32_t scale)
{
    if (!lbl) return;
    lbl->font_scale = scale;
    if (lbl->text[0]) {
        lbl->base.w = gfx_text_width(lbl->text, scale) + 8;
        lbl->base.h = FONT_H * scale + 4;
    }
}

void gui_input_set_text(textinput_widget_t* inp, const char* text)
{
    if (!inp || !text) return;
    strncpy(inp->text, text, inp->max_len);
    inp->text[inp->max_len] = '\0';
    inp->cursor_pos = (uint32_t)strlen(inp->text);
}

const char* gui_input_get_text(textinput_widget_t* inp)
{
    return inp ? inp->text : NULL;
}

void gui_list_add(list_widget_t* lst, const char* text, const char* sub, uint32_t color)
{
    if (!lst || lst->item_count >= 64) return;
    list_item_t* item = &lst->items[lst->item_count++];
    if (text) strncpy(item->text, text, sizeof(item->text) - 1);
    if (sub)  strncpy(item->sub,  sub,  sizeof(item->sub)  - 1);
    item->icon_color = color ? color : C_CRIMSON;
}

void gui_list_clear(list_widget_t* lst)
{
    if (!lst) return;
    lst->item_count = 0;
    lst->selected_idx = 0;
}

void gui_progress_set(progress_widget_t* pr, uint32_t pct)
{
    if (!pr) return;
    pr->progress = (pct > 100) ? 100 : pct;
    pr->indeterminate = 0;
}

void gui_progress_set_indeterminate(progress_widget_t* pr)
{
    if (!pr) return;
    pr->indeterminate = 1;
}

void gui_widget_set_visible(void* widget, uint32_t visible)
{
    if (!widget) return;
    ((widget_t*)widget)->visible = visible;
}

void gui_widget_set_enabled(void* widget, uint32_t enabled)
{
    if (!widget) return;
    ((widget_t*)widget)->enabled = enabled;
}

void gui_widget_move(void* widget, int32_t x, int32_t y)
{
    if (!widget) return;
    widget_t* w = (widget_t*)widget;
    w->x = x;
    w->y = y;
}

void gui_widget_set_size(void* widget, uint32_t w, uint32_t h)
{
    if (!widget) return;
    widget_t* wg = (widget_t*)widget;
    wg->w = w;
    wg->h = h;
}

void gui_widget_set_color(void* widget, uint32_t bg, uint32_t fg)
{
    if (!widget) return;
    widget_t* w = (widget_t*)widget;
    w->bg_color = bg;
    w->fg_color = fg;
}

void gui_widget_destroy(void* widget)
{
    if (!widget) return;
    /* Remove from pool */
    for (uint32_t i = 0; i < widget_count; i++) {
        if (widget_pool[i] == widget) {
            kfree(widget);
            /* Shift remaining */
            for (uint32_t j = i; j < widget_count - 1; j++)
                widget_pool[j] = widget_pool[j + 1];
            widget_count--;
            return;
        }
    }
}

void gui_widgets_clear(void)
{
    for (uint32_t i = 0; i < widget_count; i++) {
        kfree(widget_pool[i]);
        widget_pool[i] = NULL;
    }
    widget_count = 0;
}

uint32_t gui_widget_count(void)
{
    return widget_count;
}
