/* Copyright (c) 2026 SynChanCyberSecurity LLC. All Rights Reserved. */
#ifndef _CRIMSON_TOUCH_H
#define _CRIMSON_TOUCH_H

#include <crimson/types.h>

#define GESTURE_NONE            0
#define GESTURE_TAP             1
#define GESTURE_DOUBLE_TAP      2
#define GESTURE_LONG_PRESS      3
#define GESTURE_SWIPE_LEFT      4
#define GESTURE_SWIPE_RIGHT     5
#define GESTURE_SWIPE_UP        6
#define GESTURE_SWIPE_DOWN      7
#define GESTURE_PINCH_IN        8
#define GESTURE_PINCH_OUT       9
#define GESTURE_ROTATE_CW       10
#define GESTURE_ROTATE_CCW      11

#define EVENT_DOWN              0
#define EVENT_UP                1
#define EVENT_CONTACT           2

typedef struct {
    uint32_t id;
    uint32_t x;
    uint32_t y;
    uint32_t pressure;
    uint32_t width;
    uint32_t event;
    uint64_t timestamp;
} touch_event_t;

void touch_init(void);
int touch_read_event(touch_event_t* out);
int touch_read_event_nb(touch_event_t* out);
uint32_t touch_get_active_points(void);
int touch_get_point(uint32_t id, uint32_t* x, uint32_t* y, uint32_t* pressure);
uint32_t touch_get_gesture(void);
void touch_set_calibration(int32_t x_min, int32_t x_max,
                            int32_t y_min, int32_t y_max,
                            int32_t screen_w, int32_t screen_h);
int touch_fw_update(const uint8_t* firmware, size_t len);

#endif
