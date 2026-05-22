#ifndef _CRIMSON_WIFI_H
#define _CRIMSON_WIFI_H

#include <crimson/types.h>

typedef struct wifi_interface wifi_interface_t;

#define WIFI_MODE_STA       0
#define WIFI_MODE_AP        1
#define WIFI_MODE_MONITOR   2
#define WIFI_MODE_MESH      3

#define WIFI_DISCONNECTED   0
#define WIFI_SCANNING       1
#define WIFI_AUTHENTICATING 2
#define WIFI_ASSOCIATING    3
#define WIFI_4WAY_HANDSHAKE 4
#define WIFI_CONNECTED      5

void wifi_init(void);
wifi_interface_t* wifi_create_interface(struct wifi_driver* drv);
int wifi_scan(wifi_interface_t* iface);
int wifi_get_scan_results(wifi_interface_t* iface, void* results, uint32_t max);
int wifi_connect(wifi_interface_t* iface, const char* ssid, const char* key);
int wifi_disconnect(wifi_interface_t* iface);
int wifi_start_ap(wifi_interface_t* iface, const char* ssid, const char* key, uint8_t ch);
int wifi_start_monitor(wifi_interface_t* iface, void (*cb)(const uint8_t* f, uint32_t l, int32_t r));
int wifi_tx_frame(wifi_interface_t* iface, const uint8_t* frame, uint32_t len);
int32_t wifi_get_rssi(wifi_interface_t* iface);
void wifi_set_power_save(wifi_interface_t* iface, uint32_t mode);

#endif
