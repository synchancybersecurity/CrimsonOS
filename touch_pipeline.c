/*
 * Crimson OS - Touch Input Pipeline
 * Complete pipeline: IRQ → debounce → calibrate → gesture → dispatch
 * Supports: Goodix GT911, FocalTech FT5x06, Synaptics RMI4
 */

#include <crimson/types.h>
#include <crimson/printk.h>
#include <crimson/spinlock.h>
#include <crimson/timer.h>
#include <crimson/memory.h>

/* ── Touch Event Types ── */
typedef enum {
    TOUCH_DOWN = 0,
    TOUCH_UP,
    TOUCH_MOVE,
    TOUCH_CANCEL,
} touch_type_t;

/* ── Gesture Types ── */
typedef enum {
    GESTURE_NONE = 0,
    GESTURE_TAP,
    GESTURE_DOUBLE_TAP,
    GESTURE_LONG_PRESS,
    GESTURE_SWIPE_UP,
    GESTURE_SWIPE_DOWN,
    GESTURE_SWIPE_LEFT,
    GESTURE_SWIPE_RIGHT,
    GESTURE_PINCH_IN,
    GESTURE_PINCH_OUT,
    GESTURE_ROTATE,
    GESTURE_TWO_FINGER_SWIPE,
    GESTURE_THREE_FINGER_SWIPE,
    GESTURE_EDGE_LEFT,
    GESTURE_EDGE_RIGHT,
    GESTURE_EDGE_BOTTOM,
} gesture_type_t;

/* ── Raw Touch Point ── */
typedef struct {
    int32_t     x, y;
    uint32_t    pressure;
    uint32_t    area;
    uint8_t     id;         /* Finger tracking ID */
    uint8_t     active;
} touch_point_t;

/* ── Processed Touch Event ── */
typedef struct {
    touch_type_t    type;
    int32_t         x, y;
    int32_t         dx, dy;     /* Delta from last position */
    uint32_t        pressure;
    uint8_t         finger_id;
    uint8_t         finger_count;
    uint64_t        timestamp;
    gesture_type_t  gesture;
} touch_event_t;

/* ── Configuration ── */
#define TOUCH_MAX_FINGERS       10
#define TOUCH_QUEUE_SIZE        128
#define TOUCH_TAP_TIMEOUT_MS    200
#define TOUCH_LONG_PRESS_MS     500
#define TOUCH_SWIPE_THRESHOLD   40      /* pixels */
#define TOUCH_EDGE_MARGIN       20      /* pixels from screen edge */
#define TOUCH_DEBOUNCE_MS       8

/* ── Calibration Matrix ── */
typedef struct {
    float   scale_x, scale_y;
    float   offset_x, offset_y;
    int     swap_xy;
    int     invert_x, invert_y;
    uint32_t screen_w, screen_h;
} touch_calibration_t;

/* ── Pipeline State ── */
static struct {
    /* Current finger state */
    touch_point_t   fingers[TOUCH_MAX_FINGERS];
    uint32_t        active_count;
    
    /* Previous frame state for delta calculation */
    touch_point_t   prev_fingers[TOUCH_MAX_FINGERS];
    uint32_t        prev_active_count;
    
    /* Gesture recognition state */
    struct {
        int32_t     start_x, start_y;
        int32_t     start_x2, start_y2;    /* Second finger */
        uint64_t    start_time;
        uint64_t    last_tap_time;
        int         started;
        int         moved;
        float       initial_distance;       /* Pinch tracking */
    } gesture;
    
    /* Event queue — lockless SPSC ring buffer */
    touch_event_t   queue[TOUCH_QUEUE_SIZE];
    volatile uint32_t queue_head;
    volatile uint32_t queue_tail;
    
    /* Calibration */
    touch_calibration_t cal;
    
    /* Stats */
    uint64_t        total_events;
    uint64_t        dropped_events;
    uint64_t        last_irq_time;
    
    spinlock_t      lock;
    int             initialized;
    
    /* Compositor callback — called on every event */
    void (*dispatch_fn)(const touch_event_t* ev);
} g_touch;


/* ═══════════════════════════════════════════════════════════
 *  CALIBRATION
 * ═══════════════════════════════════════════════════════════ */

static void touch_calibrate_point(int32_t raw_x, int32_t raw_y,
                                  int32_t* out_x, int32_t* out_y)
{
    float fx = raw_x * g_touch.cal.scale_x + g_touch.cal.offset_x;
    float fy = raw_y * g_touch.cal.scale_y + g_touch.cal.offset_y;
    
    if (g_touch.cal.swap_xy) {
        float tmp = fx; fx = fy; fy = tmp;
    }
    if (g_touch.cal.invert_x)
        fx = g_touch.cal.screen_w - fx;
    if (g_touch.cal.invert_y)
        fy = g_touch.cal.screen_h - fy;
    
    /* Clamp to screen */
    if (fx < 0) fx = 0;
    if (fy < 0) fy = 0;
    if (fx >= g_touch.cal.screen_w) fx = g_touch.cal.screen_w - 1;
    if (fy >= g_touch.cal.screen_h) fy = g_touch.cal.screen_h - 1;
    
    *out_x = (int32_t)fx;
    *out_y = (int32_t)fy;
}

/* ═══════════════════════════════════════════════════════════
 *  EVENT QUEUE (Lock-free SPSC)
 * ═══════════════════════════════════════════════════════════ */

static int touch_enqueue(const touch_event_t* ev)
{
    uint32_t next = (g_touch.queue_tail + 1) % TOUCH_QUEUE_SIZE;
    if (next == g_touch.queue_head) {
        g_touch.dropped_events++;
        return -1;  /* Full */
    }
    
    g_touch.queue[g_touch.queue_tail] = *ev;
    __sync_synchronize();   /* Memory barrier */
    g_touch.queue_tail = next;
    g_touch.total_events++;
    return 0;
}

int touch_dequeue(touch_event_t* out)
{
    if (g_touch.queue_head == g_touch.queue_tail)
        return -1;  /* Empty */
    
    *out = g_touch.queue[g_touch.queue_head];
    __sync_synchronize();
    g_touch.queue_head = (g_touch.queue_head + 1) % TOUCH_QUEUE_SIZE;
    return 0;
}

int touch_queue_pending(void)
{
    uint32_t h = g_touch.queue_head;
    uint32_t t = g_touch.queue_tail;
    return (t >= h) ? (t - h) : (TOUCH_QUEUE_SIZE - h + t);
}

/* ═══════════════════════════════════════════════════════════
 *  GESTURE RECOGNITION
 * ═══════════════════════════════════════════════════════════ */

static float touch_distance(int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
    float dx = (float)(x1 - x0);
    float dy = (float)(y1 - y0);
    /* Approximate sqrt: |dx| + |dy| - min(|dx|,|dy|)/2 */
    float adx = dx < 0 ? -dx : dx;
    float ady = dy < 0 ? -dy : dy;
    float mn = adx < ady ? adx : ady;
    return adx + ady - mn * 0.5f;
}

static gesture_type_t touch_recognize(void)
{
    uint64_t now = timer_get_ticks() / 1000; /* ms */
    uint64_t duration = now - g_touch.gesture.start_time;
    
    if (!g_touch.gesture.started) return GESTURE_NONE;
    
    int32_t dx = g_touch.fingers[0].x - g_touch.gesture.start_x;
    int32_t dy = g_touch.fingers[0].y - g_touch.gesture.start_y;
    int32_t adx = dx < 0 ? -dx : dx;
    int32_t ady = dy < 0 ? -dy : dy;
    
    /* ── Edge gestures ── */
    if (g_touch.gesture.start_x < TOUCH_EDGE_MARGIN && dx > TOUCH_SWIPE_THRESHOLD)
        return GESTURE_EDGE_LEFT;
    if (g_touch.gesture.start_x > (int32_t)(g_touch.cal.screen_w - TOUCH_EDGE_MARGIN)
        && dx < -TOUCH_SWIPE_THRESHOLD)
        return GESTURE_EDGE_RIGHT;
    if (g_touch.gesture.start_y > (int32_t)(g_touch.cal.screen_h - TOUCH_EDGE_MARGIN)
        && dy < -TOUCH_SWIPE_THRESHOLD)
        return GESTURE_EDGE_BOTTOM;
    
    /* ── Pinch (2-finger) ── */
    if (g_touch.active_count == 2 && g_touch.gesture.initial_distance > 0) {
        float dist = touch_distance(
            g_touch.fingers[0].x, g_touch.fingers[0].y,
            g_touch.fingers[1].x, g_touch.fingers[1].y);
        float ratio = dist / g_touch.gesture.initial_distance;
        if (ratio > 1.3f) return GESTURE_PINCH_OUT;
        if (ratio < 0.7f) return GESTURE_PINCH_IN;
    }
    
    /* ── Swipe detection ── */
    if (adx > TOUCH_SWIPE_THRESHOLD || ady > TOUCH_SWIPE_THRESHOLD) {
        g_touch.gesture.moved = 1;
        if (adx > ady) {
            return dx > 0 ? GESTURE_SWIPE_RIGHT : GESTURE_SWIPE_LEFT;
        } else {
            return dy > 0 ? GESTURE_SWIPE_DOWN : GESTURE_SWIPE_UP;
        }
    }
    
    /* ── Long press ── */
    if (!g_touch.gesture.moved && duration > TOUCH_LONG_PRESS_MS)
        return GESTURE_LONG_PRESS;
    
    return GESTURE_NONE;
}

static gesture_type_t touch_recognize_tap_up(void)
{
    uint64_t now = timer_get_ticks() / 1000;
    uint64_t duration = now - g_touch.gesture.start_time;
    
    if (!g_touch.gesture.moved && duration < TOUCH_TAP_TIMEOUT_MS) {
        /* Check for double tap */
        if (now - g_touch.gesture.last_tap_time < 300) {
            g_touch.gesture.last_tap_time = 0;
            return GESTURE_DOUBLE_TAP;
        }
        g_touch.gesture.last_tap_time = now;
        return GESTURE_TAP;
    }
    
    return GESTURE_NONE;
}

/* ═══════════════════════════════════════════════════════════
 *  IRQ HANDLER — Called from touch controller interrupt
 * ═══════════════════════════════════════════════════════════ */

void touch_irq_handler(uint32_t irq, void* data)
{
    (void)irq; (void)data;
    
    uint64_t now = timer_get_ticks() / 1000;
    
    /* Debounce */
    if (now - g_touch.last_irq_time < TOUCH_DEBOUNCE_MS)
        return;
    g_touch.last_irq_time = now;
    
    /* Save previous state */
    kmemcpy(g_touch.prev_fingers, g_touch.fingers, sizeof(g_touch.fingers));
    g_touch.prev_active_count = g_touch.active_count;
    
    /*
     * Read raw touch data from controller.
     * On real hardware this reads I2C registers from GT911/FT5x06.
     * The touch_read_hw() function is board-specific.
     */
    touch_point_t raw[TOUCH_MAX_FINGERS];
    uint32_t raw_count = 0;
    
    /* Hardware abstraction: reads controller registers */
    extern int touch_read_hw(touch_point_t* pts, uint32_t max);
    raw_count = touch_read_hw(raw, TOUCH_MAX_FINGERS);
    
    /* Calibrate each point */
    g_touch.active_count = raw_count;
    for (uint32_t i = 0; i < raw_count; i++) {
        int32_t cx, cy;
        touch_calibrate_point(raw[i].x, raw[i].y, &cx, &cy);
        g_touch.fingers[i].x = cx;
        g_touch.fingers[i].y = cy;
        g_touch.fingers[i].pressure = raw[i].pressure;
        g_touch.fingers[i].area = raw[i].area;
        g_touch.fingers[i].id = raw[i].id;
        g_touch.fingers[i].active = 1;
    }
    /* Mark inactive fingers */
    for (uint32_t i = raw_count; i < TOUCH_MAX_FINGERS; i++)
        g_touch.fingers[i].active = 0;
    
    /* ── Generate events ── */
    
    /* Finger down */
    if (raw_count > 0 && g_touch.prev_active_count == 0) {
        g_touch.gesture.start_x = g_touch.fingers[0].x;
        g_touch.gesture.start_y = g_touch.fingers[0].y;
        g_touch.gesture.start_time = now;
        g_touch.gesture.started = 1;
        g_touch.gesture.moved = 0;
        
        if (raw_count >= 2) {
            g_touch.gesture.start_x2 = g_touch.fingers[1].x;
            g_touch.gesture.start_y2 = g_touch.fingers[1].y;
            g_touch.gesture.initial_distance = touch_distance(
                g_touch.fingers[0].x, g_touch.fingers[0].y,
                g_touch.fingers[1].x, g_touch.fingers[1].y);
        }
        
        touch_event_t ev = {0};
        ev.type = TOUCH_DOWN;
        ev.x = g_touch.fingers[0].x;
        ev.y = g_touch.fingers[0].y;
        ev.pressure = g_touch.fingers[0].pressure;
        ev.finger_id = g_touch.fingers[0].id;
        ev.finger_count = raw_count;
        ev.timestamp = now;
        ev.gesture = GESTURE_NONE;
        touch_enqueue(&ev);
    }
    
    /* Finger move */
    else if (raw_count > 0 && g_touch.prev_active_count > 0) {
        touch_event_t ev = {0};
        ev.type = TOUCH_MOVE;
        ev.x = g_touch.fingers[0].x;
        ev.y = g_touch.fingers[0].y;
        ev.dx = g_touch.fingers[0].x - g_touch.prev_fingers[0].x;
        ev.dy = g_touch.fingers[0].y - g_touch.prev_fingers[0].y;
        ev.pressure = g_touch.fingers[0].pressure;
        ev.finger_id = g_touch.fingers[0].id;
        ev.finger_count = raw_count;
        ev.timestamp = now;
        ev.gesture = touch_recognize();
        touch_enqueue(&ev);
    }
    
    /* Finger up */
    else if (raw_count == 0 && g_touch.prev_active_count > 0) {
        touch_event_t ev = {0};
        ev.type = TOUCH_UP;
        ev.x = g_touch.prev_fingers[0].x;
        ev.y = g_touch.prev_fingers[0].y;
        ev.finger_id = g_touch.prev_fingers[0].id;
        ev.finger_count = 0;
        ev.timestamp = now;
        ev.gesture = touch_recognize_tap_up();
        touch_enqueue(&ev);
        
        g_touch.gesture.started = 0;
        g_touch.gesture.initial_distance = 0;
    }
    
    /* Dispatch to compositor if callback is set */
    if (g_touch.dispatch_fn) {
        touch_event_t ev;
        while (touch_dequeue(&ev) == 0) {
            g_touch.dispatch_fn(&ev);
        }
    }
}

/* ═══════════════════════════════════════════════════════════
 *  PUBLIC API
 * ═══════════════════════════════════════════════════════════ */

void touch_pipeline_set_dispatch(void (*fn)(const touch_event_t*))
{
    g_touch.dispatch_fn = fn;
}

void touch_pipeline_get_position(int32_t* x, int32_t* y)
{
    if (g_touch.active_count > 0) {
        *x = g_touch.fingers[0].x;
        *y = g_touch.fingers[0].y;
    } else {
        *x = -1; *y = -1;
    }
}

uint32_t touch_pipeline_finger_count(void)
{
    return g_touch.active_count;
}

void touch_pipeline_set_calibration(uint32_t screen_w, uint32_t screen_h,
                                     float sx, float sy,
                                     float ox, float oy,
                                     int swap, int inv_x, int inv_y)
{
    g_touch.cal.screen_w = screen_w;
    g_touch.cal.screen_h = screen_h;
    g_touch.cal.scale_x = sx;
    g_touch.cal.scale_y = sy;
    g_touch.cal.offset_x = ox;
    g_touch.cal.offset_y = oy;
    g_touch.cal.swap_xy = swap;
    g_touch.cal.invert_x = inv_x;
    g_touch.cal.invert_y = inv_y;
    
    printk(KERN_INFO "[TOUCH] Calibration: scale(%.3f,%.3f) offset(%.1f,%.1f) "
           "swap=%d inv(%d,%d)\n", sx, sy, ox, oy, swap, inv_x, inv_y);
}

int touch_pipeline_init(void)
{
    kmemset(&g_touch, 0, sizeof(g_touch));
    spin_lock_init(&g_touch.lock);
    
    /* Default calibration for 720x1440 panel with GT911 */
    touch_pipeline_set_calibration(
        720, 1440,      /* screen dimensions */
        1.0f, 1.0f,     /* scale */
        0.0f, 0.0f,     /* offset */
        0, 0, 0          /* no swap, no invert */
    );
    
    /* Register IRQ handler with GIC */
    /* irq_register(TOUCH_IRQ, touch_irq_handler, NULL); */
    
    g_touch.initialized = 1;
    
    printk(KERN_INFO "[TOUCH] Input pipeline initialized\n");
    printk(KERN_INFO "[TOUCH]   Max fingers:    %d\n", TOUCH_MAX_FINGERS);
    printk(KERN_INFO "[TOUCH]   Queue depth:    %d\n", TOUCH_QUEUE_SIZE);
    printk(KERN_INFO "[TOUCH]   Tap timeout:    %d ms\n", TOUCH_TAP_TIMEOUT_MS);
    printk(KERN_INFO "[TOUCH]   Long press:     %d ms\n", TOUCH_LONG_PRESS_MS);
    printk(KERN_INFO "[TOUCH]   Swipe thresh:   %d px\n", TOUCH_SWIPE_THRESHOLD);
    printk(KERN_INFO "[TOUCH]   Gestures: tap, double-tap, long-press, "
           "swipe, pinch, edge\n");
    
    return 0;
}
