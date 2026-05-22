#ifndef _CRIMSON_WIFI_H
#define _CRIMSON_WIFI_H

#include <crimson/types.h>
#include <crimson/spinlock.h>
#include <crimson/net.h>

#define WIFI_MAX_INTERFACES     4
#define WIFI_MAX_SCAN_RESULTS   128
#define WIFI_MAX_SSID_LEN       32
#define WIFI_MAX_KEY_LEN        64

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

typedef struct {
    uint8_t  bssid[6];
    uint8_t  ssid[WIFI_MAX_SSID_LEN + 1];
    int32_t  rssi;
    uint32_t freq;
    uint32_t beacon_int;
    uint32_t caps;
    uint8_t  channel;
    uint8_t  ht:1, vht:1, he:1;
    uint8_t  security;
    uint32_t akms;
} wifi_scan_result_t;

typedef struct wifi_driver wifi_driver_t;

typedef struct wifi_interface {
    uint32_t index;
    uint32_t mode;
    uint32_t state;
    uint8_t  mac[6];
    uint8_t  connected_bssid[6];
    uint8_t  connected_ssid[WIFI_MAX_SSID_LEN + 1];
    uint32_t auth_type;
    uint8_t  pmk[32];
    uint32_t freq;
    uint32_t bw;
    int32_t  last_rssi;
    uint8_t  ap_ssid[WIFI_MAX_SSID_LEN + 1];
    uint8_t  ap_channel;
    uint32_t ap_num_sta;
    uint32_t monitor_active;
    void (*monitor_rx_cb)(const uint8_t* frame, uint32_t len, int32_t rssi);
    wifi_scan_result_t scan_results[WIFI_MAX_SCAN_RESULTS];
    uint32_t scan_count;
    uint32_t power_save;
    uint32_t dtim_period;
    wifi_driver_t* driver;
    void* driver_priv;
    spinlock_t lock;
} wifi_interface_t;

struct wifi_driver {
    const char* name;
    int  (*probe)(wifi_interface_t* iface);
    void (*remove)(wifi_interface_t* iface);
    int  (*tx_frame)(wifi_interface_t* iface, const uint8_t* frame, uint32_t len);
    int  (*set_channel)(wifi_interface_t* iface, uint32_t freq, uint32_t bw);
    int  (*set_mode)(wifi_interface_t* iface, uint32_t mode);
    int  (*start_scan)(wifi_interface_t* iface);
    int  (*connect)(wifi_interface_t* iface, const uint8_t* ssid, const uint8_t* key);
    int  (*disconnect)(wifi_interface_t* iface);
    int  (*set_power_save)(wifi_interface_t* iface, uint32_t ps_mode);
    int  (*set_txpower)(wifi_interface_t* iface, int32_t dbm);
};

void wifi_init(void);
wifi_interface_t* wifi_create_interface(wifi_driver_t* drv);
int wifi_scan(wifi_interface_t* iface);
int wifi_get_scan_results(wifi_interface_t* iface, wifi_scan_result_t* results, uint32_t max);
int wifi_connect(wifi_interface_t* iface, const char* ssid, const char* key);
int wifi_disconnect(wifi_interface_t* iface);
int wifi_start_ap(wifi_interface_t* iface, const char* ssid, const char* key, uint8_t ch);
int wifi_start_monitor(wifi_interface_t* iface, void (*cb)(const uint8_t* f, uint32_t l, int32_t r));
int wifi_tx_frame(wifi_interface_t* iface, const uint8_t* frame, uint32_t len);
int32_t wifi_get_rssi(wifi_interface_t* iface);
void wifi_set_power_save(wifi_interface_t* iface, uint32_t mode);
void wifi_rx_frame(wifi_interface_t* iface, const uint8_t* frame, uint32_t len, int32_t rssi);

#endif
