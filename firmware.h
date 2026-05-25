/* Copyright (c) 2026 SynChanCyberSecurity LLC. All Rights Reserved. */
/*
 * Crimson OS - Firmware & Radio Integration Header
 */

#ifndef _CRIMSON_FIRMWARE_H
#define _CRIMSON_FIRMWARE_H

#include <crimson/types.h>

/* ── Firmware Loader ── */
int     firmware_load(const char* name, void** out_data, size_t* out_size);

/* ── WiFi ── */
int     wifi_init(void);
int     wifi_scan(void);
int     wifi_connect(const char* ssid, const char* password);
int     wifi_disconnect(void);

/* ── Cellular (QMI) ── */
int     cellular_init(void);
int     cellular_connect(const char* apn);
int     cellular_get_signal(void);
int     cellular_send_sms(const char* number, const char* message);
void    cellular_shutdown(void);

#endif
