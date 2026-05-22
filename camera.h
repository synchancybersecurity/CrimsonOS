#ifndef _CRIMSON_CAMERA_H
#define _CRIMSON_CAMERA_H

#include <crimson/types.h>

#define FLASH_OFF       0
#define FLASH_ON        1
#define FLASH_AUTO      2
#define FLASH_TORCH     3

#define FOCUS_AUTO      0
#define FOCUS_MACRO     1
#define FOCUS_INFINITY  2
#define FOCUS_CONTINUOUS 3
#define FOCUS_MANUAL    4

void camera_init(void);
int camera_detect_sensors(void);
int camera_open_preview(uint32_t w, uint32_t h, uint32_t fps,
                         void (*cb)(const uint8_t* d, uint32_t l, uint32_t ts));
int camera_capture(uint32_t w, uint32_t h, uint32_t quality, uint8_t* out, uint32_t* len);
int camera_start_video(uint32_t w, uint32_t h, uint32_t fps, uint32_t bitrate,
                        void (*cb)(const uint8_t* d, uint32_t l, uint32_t ts));
void camera_stop_video(void);
void camera_set_flash(uint32_t mode);
void camera_set_focus_mode(uint32_t mode);
void camera_focus_trigger(void);

#endif
