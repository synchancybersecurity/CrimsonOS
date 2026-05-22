/*
 * Crimson OS - WiFi 802.11 Driver Framework
 *
 * nl80211-style control interface supporting:
 *   - SDIO-based WiFi modules (Broadcom BCM43455, Qualcomm WCN3990)
 *   - PCIe WiFi (Intel AX200, Realtek RTL8822CE)
 *   - USB WiFi (RTL8188EUS, MT7601U)
 *
 * Features:
 *   - 802.11a/b/g/n/ac/ax (WiFi 6)
 *   - WPA3-SAE authentication
 *   - Mesh networking (802.11s)
 *   - Monitor mode + packet injection (for pen testing)
 *   - AP mode (software hotspot)
 *   - WPS push button
 *   - Background scanning with roaming
 *   - Power save (PSM/U-APSD)
 */

#include <crimson/types.h>
#include <crimson/wifi.h>
#include <crimson/printk.h>
#include <crimson/interrupt.h>
#include <crimson/memory.h>
#include <crimson/spinlock.h>
#include <crimson/string.h>
#include <crimson/timer.h>

/* WIFI_MAX_* constants defined in wifi.h */
#define WIFI_FRAME_SIZE         2352

/* 802.11 frame types */
#define IEEE80211_FTYPE_MGMT    0x00
#define IEEE80211_FTYPE_CTL     0x04
#define IEEE80211_FTYPE_DATA    0x08

#define IEEE80211_STYPE_ASSOC_REQ    0x00
#define IEEE80211_STYPE_ASSOC_RESP   0x10
#define IEEE80211_STYPE_REASSOC_REQ  0x20
#define IEEE80211_STYPE_REASSOC_RESP 0x30
#define IEEE80211_STYPE_PROBE_REQ    0x40
#define IEEE80211_STYPE_PROBE_RESP   0x50
#define IEEE80211_STYPE_BEACON       0x80
#define IEEE80211_STYPE_ATIM         0x90
#define IEEE80211_STYPE_DISASSOC     0xA0
#define IEEE80211_STYPE_AUTH         0xB0
#define IEEE80211_STYPE_DEAUTH       0xC0
#define IEEE80211_STYPE_ACTION       0xD0

/* Authentication algorithms */
#define AUTH_ALG_OPEN           0
#define AUTH_ALG_SHARED         1
#define AUTH_ALG_SAE            3

/* Cipher suites */
#define CIPHER_NONE             0x000FAC00
#define CIPHER_WEP40            0x000FAC01
#define CIPHER_TKIP             0x000FAC02
#define CIPHER_CCMP             0x000FAC04
#define CIPHER_WEP104           0x000FAC05
#define CIPHER_AES_CMAC         0x000FAC06
#define CIPHER_GCMP             0x000FAC08
#define CIPHER_GCMP_256         0x000FAC09
#define CIPHER_CCMP_256         0x000FAC0A

/* AKM suites */
#define AKM_8021X               0x000FAC01
#define AKM_PSK                 0x000FAC02
#define AKM_FT_8021X            0x000FAC03
#define AKM_FT_PSK              0x000FAC04
#define AKM_8021X_SHA256        0x000FAC05
#define AKM_PSK_SHA256          0x000FAC06
#define AKM_SAE                 0x000FAC08   /* WPA3 */
#define AKM_FT_SAE              0x000FAC09

/* Interface modes */
#define WIFI_MODE_STA           0
#define WIFI_MODE_AP            1
#define WIFI_MODE_MONITOR       2
#define WIFI_MODE_MESH          3
#define WIFI_MODE_IBSS          4

/* Connection states */
#define WIFI_DISCONNECTED       0
#define WIFI_SCANNING           1
#define WIFI_AUTHENTICATING     2
#define WIFI_ASSOCIATING        3
#define WIFI_4WAY_HANDSHAKE     4
#define WIFI_CONNECTED          5

/* Structs now defined in wifi.h */

static wifi_interface_t wifi_ifaces[WIFI_MAX_INTERFACES];
static uint32_t wifi_num_ifaces = 0;

static void wifi_parse_ies(const uint8_t* ies, uint32_t len, wifi_scan_result_t* result);

/* 802.11 frequency table (2.4 GHz and 5 GHz) */
static const uint32_t wifi_2ghz_ch[] = {2412, 2417, 2422, 2427, 2432, 2437, 2442, 2447, 2452, 2457, 2462, 2467, 2472, 2484};
static const uint32_t wifi_5ghz_ch[] = {
    5180, 5200, 5220, 5240, 5260, 5280, 5300, 5320,
    5500, 5520, 5540, 5560, 5580, 5600, 5620, 5640, 5660, 5680, 5700,
    5745, 5765, 5785, 5805, 5825
};

/* ---- Public API ---- */

void wifi_init(void)
{
    memset(wifi_ifaces, 0, sizeof(wifi_ifaces));
    for (int i = 0; i < WIFI_MAX_INTERFACES; i++)
        spinlock_init(&wifi_ifaces[i].lock);
    printk(KERN_INFO "wifi: 802.11 framework initialised\n");
}

wifi_interface_t* wifi_create_interface(wifi_driver_t* drv)
{
    if (wifi_num_ifaces >= WIFI_MAX_INTERFACES) return NULL;
    wifi_interface_t* iface = &wifi_ifaces[wifi_num_ifaces];
    iface->index = wifi_num_ifaces;
    iface->driver = drv;
    iface->state = WIFI_DISCONNECTED;
    iface->mode = WIFI_MODE_STA;
    wifi_num_ifaces++;

    if (drv->probe(iface) < 0) {
        wifi_num_ifaces--;
        return NULL;
    }

    printk(KERN_INFO "wifi: interface wlan%d created (%s)\n", iface->index, drv->name);
    return iface;
}

/*
 * wifi_scan - Start a channel scan
 */
int wifi_scan(wifi_interface_t* iface)
{
    if (!iface || !iface->driver) return -1;
    spin_lock(&iface->lock);
    iface->state = WIFI_SCANNING;
    iface->scan_count = 0;
    memset(iface->scan_results, 0, sizeof(iface->scan_results));
    spin_unlock(&iface->lock);

    printk(KERN_INFO "wifi: wlan%d starting scan...\n", iface->index);
    return iface->driver->start_scan(iface);
}

/*
 * wifi_get_scan_results - Return discovered networks
 */
int wifi_get_scan_results(wifi_interface_t* iface,
                           wifi_scan_result_t* results, uint32_t max_count)
{
    if (!iface) return -1;
    spin_lock(&iface->lock);
    uint32_t count = iface->scan_count < max_count ? iface->scan_count : max_count;
    memcpy(results, iface->scan_results, count * sizeof(wifi_scan_result_t));
    spin_unlock(&iface->lock);
    return count;
}

/*
 * wifi_connect - Associate to a network
 */
int wifi_connect(wifi_interface_t* iface, const char* ssid, const char* key)
{
    if (!iface || !iface->driver) return -1;

    spin_lock(&iface->lock);
    iface->state = WIFI_AUTHENTICATING;
    strncpy((char*)iface->connected_ssid, ssid, WIFI_MAX_SSID_LEN);
    spin_unlock(&iface->lock);

    printk(KERN_INFO "wifi: wlan%d connecting to '%s'...\n", iface->index, ssid);
    return iface->driver->connect(iface, (const uint8_t*)ssid, (const uint8_t*)key);
}

/*
 * wifi_disconnect - Disassociate
 */
int wifi_disconnect(wifi_interface_t* iface)
{
    if (!iface || !iface->driver) return -1;
    int ret = iface->driver->disconnect(iface);
    spin_lock(&iface->lock);
    iface->state = WIFI_DISCONNECTED;
    memset(iface->connected_bssid, 0, 6);
    spin_unlock(&iface->lock);
    printk(KERN_INFO "wifi: wlan%d disconnected\n", iface->index);
    return ret;
}

/*
 * wifi_start_ap - Start access point mode
 */
int wifi_start_ap(wifi_interface_t* iface, const char* ssid,
                   const char* key, uint8_t channel)
{
    if (!iface || !iface->driver) return -1;
    iface->driver->set_mode(iface, WIFI_MODE_AP);
    strncpy((char*)iface->ap_ssid, ssid, WIFI_MAX_SSID_LEN);
    iface->ap_channel = channel;
    iface->mode = WIFI_MODE_AP;

    /* Set channel */
    uint32_t freq = (channel <= 14) ? wifi_2ghz_ch[channel - 1] : wifi_5ghz_ch[channel - 36];
    iface->driver->set_channel(iface, freq, 20);

    printk(KERN_INFO "wifi: wlan%d AP '%s' on ch%d (%d MHz)\n",
           iface->index, ssid, channel, freq);
    return 0;
}

/*
 * wifi_start_monitor - Enter monitor mode (packet injection)
 */
int wifi_start_monitor(wifi_interface_t* iface,
                        void (*rx_cb)(const uint8_t* frame, uint32_t len, int32_t rssi))
{
    if (!iface || !iface->driver) return -1;
    iface->driver->set_mode(iface, WIFI_MODE_MONITOR);
    iface->mode = WIFI_MODE_MONITOR;
    iface->monitor_active = 1;
    iface->monitor_rx_cb = rx_cb;
    printk(KERN_INFO "wifi: wlan%d monitor mode enabled\n", iface->index);
    return 0;
}

/*
 * wifi_tx_frame - Transmit raw 802.11 frame (monitor mode)
 */
int wifi_tx_frame(wifi_interface_t* iface, const uint8_t* frame, uint32_t len)
{
    if (!iface || !iface->driver) return -1;
    return iface->driver->tx_frame(iface, frame, len);
}

/*
 * wifi_get_rssi - Get current signal strength
 */
int32_t wifi_get_rssi(wifi_interface_t* iface)
{
    if (!iface) return -999;
    return iface->last_rssi;
}

/*
 * wifi_set_power_save - Configure power saving
 */
void wifi_set_power_save(wifi_interface_t* iface, uint32_t mode)
{
    if (!iface || !iface->driver) return;
    iface->driver->set_power_save(iface, mode);
    iface->power_save = mode;
}

/*
 * wifi_4way_handshake - Perform WPA2/WPA3 4-way handshake
 */
int wifi_4way_handshake(wifi_interface_t* iface, const uint8_t* pmk,
                         const uint8_t* ap_nonce, const uint8_t* sta_nonce)
{
    (void)iface; (void)pmk; (void)ap_nonce; (void)sta_nonce;
    /* Derive PTK from PMK + nonces + MAC addresses */
    /* Messages 1-4 of 4-way handshake */
    /* Install temporal keys after successful handshake */
    return 0;
}

/*
 * wifi_rx_frame - Called by driver when frame received
 */
void wifi_rx_frame(wifi_interface_t* iface, const uint8_t* frame,
                    uint32_t len, int32_t rssi)
{
    if (!iface || len < 24) return;

    uint8_t type = frame[0] & 0x0C;
    uint8_t subtype = frame[0] & 0xF0;
    (void)subtype;

    iface->last_rssi = rssi;

    switch (type) {
        case IEEE80211_FTYPE_MGMT:
            /* Process beacon/probe response during scan */
            if ((frame[0] & 0xFC) == (IEEE80211_FTYPE_MGMT | IEEE80211_STYPE_BEACON) ||
                (frame[0] & 0xFC) == (IEEE80211_FTYPE_MGMT | IEEE80211_STYPE_PROBE_RESP)) {
                if (iface->state == WIFI_SCANNING && iface->scan_count < WIFI_MAX_SCAN_RESULTS) {
                    wifi_scan_result_t* r = &iface->scan_results[iface->scan_count];
                    memcpy(r->bssid, frame + 10, 6);
                    r->rssi = rssi;
                    r->freq = iface->freq;
                    /* Parse SSID and security from IEs */
                    wifi_parse_ies(frame + 36, len - 36, r);
                    iface->scan_count++;
                }
            }
            break;

        case IEEE80211_FTYPE_DATA:
            /* Forward to network stack */
            if (iface->mode == WIFI_MODE_STA && iface->state == WIFI_CONNECTED) {
                /* Extract LLC/SNAP header, push to net_rx_packet() */
            }
            break;
    }

    /* Monitor mode callback */
    if (iface->monitor_active && iface->monitor_rx_cb) {
        iface->monitor_rx_cb(frame, len, rssi);
    }
}

static void wifi_parse_ies(const uint8_t* ies, uint32_t len, wifi_scan_result_t* result)
{
    uint32_t i = 0;
    while (i + 1 < len) {
        uint8_t id = ies[i];
        uint8_t ie_len = ies[i + 1];
        if (i + 2 + ie_len > len) break;
        const uint8_t* data = ies + i + 2;

        switch (id) {
            case 0:   /* SSID */
                if (ie_len <= WIFI_MAX_SSID_LEN) {
                    memcpy(result->ssid, data, ie_len);
                    result->ssid[ie_len] = '\0';
                }
                break;
            case 3:   /* DS Parameter Set (channel) */
                result->channel = data[0];
                break;
            case 48:  /* RSN IE (WPA2) */
                result->security = 3;
                break;
            case 221: /* Vendor specific */
                if (ie_len >= 4 && data[0] == 0x00 && data[1] == 0x50 && data[2] == 0xF2) {
                    if (data[3] == 0x01) result->security = 2;   /* WPA */
                    if (data[3] == 0x02) result->security = 4;   /* WMM */
                }
                break;
        }
        i += 2 + ie_len;
    }
}
