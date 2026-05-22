#ifndef _CRIMSON_NET_H
struct net_if;  /* forward declaration */
#define _CRIMSON_NET_H

#include <crimson/types.h>

typedef struct {
    uint8_t  bssid[6];
    uint8_t  ssid[32];
    int32_t  rssi;
    uint32_t freq;
    uint32_t beacon_int;
    uint32_t caps;
    uint8_t  channel;
    uint8_t  ht, vht, he;
    uint8_t  security;
    uint32_t akms;
} wifi_scan_result_t;

typedef struct wifi_interface wifi_interface_t;

void net_init(void);
void net_register_if(struct net_if* dev);
void net_rx_packet(struct net_if* dev, const uint8_t* pkt, uint32_t len);

/* TCP Socket API */
int tcp_socket_create(void);
int tcp_bind(int sock_fd, uint32_t ip, uint16_t port);
int tcp_connect(int sock_fd, uint32_t ip, uint16_t port);
int tcp_listen(int sock_fd, int backlog);
int tcp_accept(int sock_fd, uint32_t* remote_ip, uint16_t* remote_port);
int tcp_send(int sock_fd, const uint8_t* data, uint32_t len);
int tcp_recv(int sock_fd, uint8_t* buf, uint32_t len);
void tcp_close(int sock_fd);

/* DHCP */
void dhcp_start(struct net_if* dev);

/* DNS */
uint32_t net_dns_resolve(const char* hostname);

/* Rx injection (for WiFi/cellular drivers) */
struct net_if;
void net_rx_frame(struct net_if* dev, const uint8_t* data, uint32_t len);

/* Helpers */
static inline uint16_t htons(uint16_t v) { return ((v&0xFF)<<8)|((v>>8)&0xFF); }
static inline uint16_t ntohs(uint16_t v) { return htons(v); }
static inline uint32_t htonl(uint32_t v) {
    return ((v&0xFF)<<24)|((v&0xFF00)<<8)|((v>>8)&0xFF00)|((v>>24)&0xFF);
}

#endif
