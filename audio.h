#ifndef _CRIMSON_AUDIO_H
#define _CRIMSON_AUDIO_H

#include <crimson/types.h>

typedef struct audio_stream audio_stream_t;

#define AUDIO_STREAM_MEDIA      0
#define AUDIO_STREAM_VOICE      1
#define AUDIO_STREAM_SYSTEM     2
#define AUDIO_STREAM_ALARM      3
#define AUDIO_STREAM_NOTIFICATION 4
#define AUDIO_STREAM_RINGTONE   5
#define AUDIO_STREAM_BLUETOOTH  6

#define CALL_ROUTE_HANDSET      0
#define CALL_ROUTE_SPEAKER      1
#define CALL_ROUTE_HEADSET      2
#define CALL_ROUTE_BLUETOOTH    3

void audio_init(void);
audio_stream_t* audio_open_stream(uint32_t type, uint32_t rate, uint32_t ch, uint32_t fmt);
int audio_write(audio_stream_t* s, const int16_t* samples, uint32_t count);
void audio_close_stream(audio_stream_t* s);
void audio_set_stream_volume(audio_stream_t* s, uint32_t vol);
void audio_set_master_volume(uint32_t vol);
void audio_play_system_sound(const int16_t* samples, uint32_t count);
void audio_set_call_route(uint32_t route);
void audio_start_call(void);
void audio_end_call(void);

#endif
