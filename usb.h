/* Copyright (c) 2026 SynChanCyberSecurity LLC. All Rights Reserved. */
#ifndef _CRIMSON_USB_H
#define _CRIMSON_USB_H

#include <crimson/types.h>

#define GADGET_FUNC_NONE        0
#define GADGET_FUNC_ADB         1
#define GADGET_FUNC_MTP         2
#define GADGET_FUNC_RNDIS       3
#define GADGET_FUNC_UMS         4
#define GADGET_FUNC_ACM         5
#define GADGET_FUNC_MIDI        6

void usb_init(void);
void usb_gadget_enable_function(uint32_t func);
void usb_gadget_disable_function(uint32_t func);
void usb_gadget_start(void);
void usb_gadget_stop(void);
void usb_gadget_set_adb_callback(void (*cb)(const uint8_t* data, uint32_t len));
int usb_gadget_adb_write(const uint8_t* data, uint32_t len);

void usb_host_init(void);
int usb_host_enumerate(uint32_t port);

#endif
