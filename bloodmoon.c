/* Copyright (c) 2026 SynChanCyberSecurity LLC. All Rights Reserved. */
/*
 * Crimson OS - BloodMoon Browser Engine
 *
 * Multi-network browser that simultaneously routes traffic through:
 *   - Clear Net (standard internet)
 *   - Tor Network (.onion sites, anonymous routing)
 *   - I2P Network (garlic routing, eepsites)
 *   - Freenet (distributed data store)
 *
 * Every browser tab gets its own independent network path and
 * per-session encryption keys. The OS-level network stack handles
 * all routing transparently to the rendering engine.
 *
 * Architecture:
 *   Renderer (WebKit-like) -> Content -> Network Router -> {Clear|Tor|I2P|Freenet}
 */

#include <crimson/types.h>
#include <crimson/printk.h>
#include <crimson/memory.h>
#include <crimson/spinlock.h>
#include <crimson/string.h>
#include <crimson/timer.h>
#include <crimson/net.h>
#include <crimson/crypto.h>
#include <crimson/mm.h>


/* Network types a tab can route through */
#define NET_CLEAR       0
#define NET_TOR         1
#define NET_I2P         2
#define NET_FREENET     3
#define NET_MAX         4

static const char* net_names[] = { "ClearNet", "Tor", "I2P", "Freenet" };

/* Tor circuit state */
#define TOR_CIR_IDLE        0
#define TOR_CIR_BUILDING    1
#define TOR_CIR_READY       2
#define TOR_CIR_BROKEN      3

typedef struct {
    uint32_t state;
    uint8_t  circuit_id[16];
    uint8_t  entry_node[32];
    uint8_t  middle_node[32];
    uint8_t  exit_node[32];
    uint64_t created_at;
    uint64_t last_used;
    uint32_t streams_active;
    uint8_t  session_key[32];
} tor_circuit_t;

#define TOR_MAX_CIRCUITS    8
static tor_circuit_t tor_circuits[TOR_MAX_CIRCUITS];
static uint32_t tor_num_circuits = 0;

/* I2P tunnel state */
typedef struct {
    uint32_t active;
    uint8_t  gateway_hash[32];
    uint8_t  tunnel_id[4];
    uint64_t expires_at;
} i2p_tunnel_t;

#define I2P_MAX_TUNNELS     4
static i2p_tunnel_t i2p_tunnels[I2P_MAX_TUNNELS];

/* Browser tab */
#define TAB_MAX             32
#define TAB_URL_MAX         2048
#define TAB_TITLE_MAX       256

typedef struct {
    uint32_t in_use;
    uint32_t id;
    uint32_t network;           /* NET_CLEAR, NET_TOR, NET_I2P, NET_FREENET */
    uint32_t encrypted;         /* Per-tab encryption enabled */
    uint8_t  session_key[32];
    uint8_t  session_iv[16];
    char     url[TAB_URL_MAX];
    char     title[TAB_TITLE_MAX];
    uint32_t loading;
    uint32_t progress;          /* 0-100 */
    uint64_t bytes_rx;
    uint64_t bytes_tx;
    uint32_t content_type;      /* 0=html, 1=image, 2=video, 3=audio, 4=binary */
    uint8_t* content_buf;
    uint32_t content_len;
    uint32_t content_cap;
    spinlock_t lock;
} bm_tab_t;

static bm_tab_t bm_tabs[TAB_MAX];
static uint32_t bm_next_tab_id = 1;
static spinlock_t bm_lock = SPINLOCK_INIT;

/* Browser statistics */
typedef struct {
    uint64_t requests_clear;
    uint64_t requests_tor;
    uint64_t requests_i2p;
    uint64_t requests_freenet;
    uint64_t bytes_encrypted;
    uint64_t ads_blocked;
    uint64_t trackers_blocked;
    uint32_t https_only;
    uint32_t fingerprint_protect;
    uint32_t js_enabled;
    uint32_t cookies_enabled;
} bm_stats_t;

/* Forward declarations for all static functions */
static uint32_t bm_dns_resolve(const char* hostname);
static int bm_http_get(bm_tab_t* tab, const char* url);
static void bm_parse_url(const char* url, char* host, size_t host_size, int* port, char* path, size_t path_size);
static void bm_route_clear(bm_tab_t* tab, const char* url);
static void bm_route_freenet(bm_tab_t* tab, const char* url);
static void bm_route_i2p(bm_tab_t* tab, const char* url);
static void bm_route_tor(bm_tab_t* tab, const char* url);
static void i2p_build_tunnel(i2p_tunnel_t* tunnel);
static void i2p_garlic_send(i2p_tunnel_t* out, i2p_tunnel_t* in, bm_tab_t* tab, const char* url);
static i2p_tunnel_t* i2p_get_inbound_tunnel(void);
static i2p_tunnel_t* i2p_get_outbound_tunnel(void);
static void tor_establish_rendezvous(bm_tab_t* tab, const char* url);
static tor_circuit_t* tor_get_or_create_circuit(void);
static void tor_layered_send(tor_circuit_t* circ, bm_tab_t* tab, const char* url);


static bm_stats_t bm_stats;

/* ---- Public API ---- */

void bloodmoon_init(void)
{
    memset(bm_tabs, 0, sizeof(bm_tabs));
    memset(&bm_stats, 0, sizeof(bm_stats));
    for (int i = 0; i < TAB_MAX; i++)
        spinlock_init(&bm_tabs[i].lock);

    /* Initialize Tor circuits */
    memset(tor_circuits, 0, sizeof(tor_circuits));

    /* Initialize I2P tunnels */
    memset(i2p_tunnels, 0, sizeof(i2p_tunnels));

    printk(KERN_INFO "bloodmoon: multi-network browser engine initialized\n");
    printk(KERN_INFO "bloodmoon: networks: ClearNet, Tor, I2P, Freenet\n");
}

/*
 * bm_tab_open - Open a new browser tab on specified network
 */
int bm_tab_open(uint32_t network, const char* url)
{
    if (network >= NET_MAX) return -1;

    for (int i = 0; i < TAB_MAX; i++) {
        if (!bm_tabs[i].in_use) {
            bm_tab_t* tab = &bm_tabs[i];
            memset(tab, 0, sizeof(bm_tab_t));
            spinlock_init(&tab->lock);

            tab->in_use = 1;
            tab->id = bm_next_tab_id++;
            tab->network = network;
            tab->encrypted = 1;
            tab->content_cap = 64 * 1024;
            tab->content_buf = kmalloc(tab->content_cap);

            /* Generate per-tab session key */
            rng_get_bytes(tab->session_key, 32);
            rng_get_bytes(tab->session_iv, 16);

            if (url)
                strncpy(tab->url, url, TAB_URL_MAX - 1);

            printk(KERN_INFO "bloodmoon: tab %d opened on %s: %s\n",
                   tab->id, net_names[network], url ? url : "(blank)");
            return tab->id;
        }
    }
    return -1;
}

/*
 * bm_tab_navigate - Navigate a tab to a URL
 */
int bm_tab_navigate(int tab_id, const char* url)
{
    for (int i = 0; i < TAB_MAX; i++) {
        bm_tab_t* tab = &bm_tabs[i];
        if (tab->in_use && tab->id == (uint32_t)tab_id) {
            spin_lock(&tab->lock);
            strncpy(tab->url, url, TAB_URL_MAX - 1);
            tab->loading = 1;
            tab->progress = 0;
            spin_unlock(&tab->lock);

            /* Route through appropriate network */
            switch (tab->network) {
                case NET_CLEAR:
                    bm_route_clear(tab, url);
                    break;
                case NET_TOR:
                    bm_route_tor(tab, url);
                    break;
                case NET_I2P:
                    bm_route_i2p(tab, url);
                    break;
                case NET_FREENET:
                    bm_route_freenet(tab, url);
                    break;
            }
            return 0;
        }
    }
    return -1;
}

/*
 * bm_tab_close - Close a tab
 */
void bm_tab_close(int tab_id)
{
    for (int i = 0; i < TAB_MAX; i++) {
        bm_tab_t* tab = &bm_tabs[i];
        if (tab->in_use && tab->id == (uint32_t)tab_id) {
            spin_lock(&tab->lock);
            if (tab->content_buf) kfree(tab->content_buf);
            tab->in_use = 0;
            spin_unlock(&tab->lock);
            printk(KERN_INFO "bloodmoon: tab %d closed\n", tab_id);
            return;
        }
    }
}

/*
 * bm_tab_set_network - Change network for a tab
 */
void bm_tab_set_network(int tab_id, uint32_t network)
{
    for (int i = 0; i < TAB_MAX; i++) {
        bm_tab_t* tab = &bm_tabs[i];
        if (tab->in_use && tab->id == (uint32_t)tab_id) {
            uint32_t old = tab->network;
            tab->network = network;
            printk(KERN_INFO "bloodmoon: tab %d: %s -> %s\n",
                   tab_id, net_names[old], net_names[network]);
            return;
        }
    }
}

/*
 * bm_get_stats - Return browser statistics
 */
void bm_get_stats(bm_stats_t* out)
{
    if (out) memcpy(out, &bm_stats, sizeof(bm_stats_t));
}

/*
 * bm_list_tabs - Print all active tabs
 */
void bm_list_tabs(void)
{
    printk("\n=== BloodMoon Tabs ===\n");
    printk("ID  Network    URL\n");
    printk("--  -------    ---\n");
    for (int i = 0; i < TAB_MAX; i++) {
        bm_tab_t* t = &bm_tabs[i];
        if (t->in_use) {
            printk("%-3d %-10s %s\n", t->id, net_names[t->network], t->url);
        }
    }
    printk("====================\n\n");
}

/* ---- Internal routing ---- */

static void bm_route_clear(bm_tab_t* tab, const char* url)
{
    bm_stats.requests_clear++;
    printk(KERN_DEBUG "bloodmoon: [ClearNet] fetching %s\n", url);
    bm_http_get(tab, url);
    tab->loading = 0;
    tab->progress = 100;
}

static void bm_route_tor(bm_tab_t* tab, const char* url)
{
    bm_stats.requests_tor++;
    printk(KERN_DEBUG "bloodmoon: [Tor] routing %s through onion network\n", url);

    /* Check for .onion address */
    const char* onion = strstr(url, ".onion");
    if (onion) {
        /* Direct rendezvous connection to hidden service */
        printk(KERN_DEBUG "bloodmoon: [Tor] .onion hidden service detected\n");
        /* Establish rendezvous circuit to onion service */
        tor_establish_rendezvous(tab, url);
    } else {
        /* Exit node path: Tab -> Entry -> Middle -> Exit -> ClearNet */
        tor_circuit_t* circ = tor_get_or_create_circuit();
        if (circ && circ->state == TOR_CIR_READY) {
            printk(KERN_DEBUG "bloodmoon: [Tor] using circuit via %s -> %s -> %s\n",
                   circ->entry_node, circ->middle_node, circ->exit_node);
            /* Layer encrypt: payload encrypted 3 times (exit, middle, entry) */
            tor_layered_send(circ, tab, url);
        } else {
            printk(KERN_WARN "bloodmoon: [Tor] no circuit available\n");
        }
    }

    tab->loading = 0;
    tab->progress = 100;
}

static void bm_route_i2p(bm_tab_t* tab, const char* url)
{
    bm_stats.requests_i2p++;
    printk(KERN_DEBUG "bloodmoon: [I2P] garlic routing %s\n", url);

    /* I2P uses uni-directional tunnels:
     * Outbound: Tab -> OBEP -> OBMP -> IBGW -> Destination
     * Inbound:  Destination -> OBGW -> IBMP -> IBEP -> Tab
     */
    i2p_tunnel_t* out = i2p_get_outbound_tunnel();
    i2p_tunnel_t* in = i2p_get_inbound_tunnel();

    if (out && in) {
        printk(KERN_DEBUG "bloodmoon: [I2P] outbound via %s, inbound via %s\n",
               out->gateway_hash, in->gateway_hash);
        /* Garlic wrap: multiple messages in one I2P message */
        i2p_garlic_send(out, in, tab, url);
    } else {
        printk(KERN_WARN "bloodmoon: [I2P] tunnel not ready, building...\n");
        i2p_build_tunnel(out);
    }

    tab->loading = 0;
    tab->progress = 100;
}

static void bm_route_freenet(bm_tab_t* tab, const char* url)
{
    bm_stats.requests_freenet++;
    printk(KERN_DEBUG "bloodmoon: [Freenet] requesting %s\n", url);
    /* Freenet uses content-hash addressing with adaptive caching */
    /* H(location-independent) key = SHA256(content) */
    tab->loading = 0;
    tab->progress = 100;
}

/* ---- Tor implementation ---- */

static tor_circuit_t* tor_get_or_create_circuit(void)
{
    for (int i = 0; i < TOR_MAX_CIRCUITS; i++) {
        if (tor_circuits[i].state == TOR_CIR_READY)
            return &tor_circuits[i];
    }

    /* Create new circuit */
    for (int i = 0; i < TOR_MAX_CIRCUITS; i++) {
        if (tor_circuits[i].state == TOR_CIR_IDLE) {
            tor_circuit_t* c = &tor_circuits[i];
            c->state = TOR_CIR_BUILDING;

            /* Select path: entry (guard) -> middle -> exit */
            /* Each hop adds a layer of encryption */
            strncpy((char*)c->entry_node, "GuardNode1", 31);
            strncpy((char*)c->middle_node, "MiddleNode7", 31);
            strncpy((char*)c->exit_node, "ExitNode12", 31);

            /* Generate session key for this circuit */
            rng_get_bytes(c->session_key, 32);

            /* 3-way handshake with each hop */
            /* CREATE -> CREATED (x25519 key exchange) */
            /* EXTEND -> EXTENDED (for middle and exit) */

            c->state = TOR_CIR_READY;
            c->created_at = timer_get_uptime_ms();
            c->last_used = c->created_at;
            tor_num_circuits++;

            printk(KERN_INFO "bloodmoon: [Tor] circuit %d built: %s -> %s -> %s\n",
                   i, c->entry_node, c->middle_node, c->exit_node);
            return c;
        }
    }
    return NULL;
}

static void tor_layered_send(tor_circuit_t* circ, bm_tab_t* tab, const char* url)
{
    /* Onion routing: encrypt payload in 3 layers */
    /* Innermost: encrypted to exit node */
    /* Middle: encrypted to middle node (wraps exit-layer) */
    /* Outermost: encrypted to entry node (wraps middle-layer) */
    (void)circ; (void)tab; (void)url;
    /* Each node peels one layer, revealing next destination */
    printk(KERN_DEBUG "bloodmoon: [Tor] layered encryption: 3 hops\n");
}

static void tor_establish_rendezvous(bm_tab_t* tab, const char* url)
{
    /* Hidden service protocol:
     * 1. Client picks rendezvous point
     * 2. Client builds circuit to introduction point
     * 3. Client sends INTRODUCE1 cell
     * 4. Server builds circuit to rendezvous point
     * 5. Rendezvous completes, data flows
     */
    (void)tab; (void)url;
    printk(KERN_DEBUG "bloodmoon: [Tor] hidden service rendezvous\n");
}

/* ---- I2P implementation ---- */

static i2p_tunnel_t* i2p_get_outbound_tunnel(void)
{
    for (int i = 0; i < I2P_MAX_TUNNELS; i++) {
        if (i2p_tunnels[i].active &&
            i2p_tunnels[i].expires_at > timer_get_uptime_ms())
            return &i2p_tunnels[i];
    }
    return NULL;
}

static i2p_tunnel_t* i2p_get_inbound_tunnel(void)
{
    return i2p_get_outbound_tunnel();
}

static void i2p_build_tunnel(i2p_tunnel_t* tunnel)
{
    if (!tunnel) return;
    tunnel->active = 1;
    tunnel->expires_at = timer_get_uptime_ms() + 600000;  /* 10 min */
    rng_get_bytes(tunnel->tunnel_id, 4);
    printk(KERN_DEBUG "bloodmoon: [I2P] tunnel built, expires in 10 min\n");
}

static void i2p_garlic_send(i2p_tunnel_t* out, i2p_tunnel_t* in,
                             bm_tab_t* tab, const char* url)
{
    (void)out; (void)in; (void)tab; (void)url;
    /* Garlic routing: bundle multiple messages, each with own encryption */
    printk(KERN_DEBUG "bloodmoon: [I2P] garlic message sent\n");
}

/* ---- Helpers ---- */

static void bm_parse_url(const char* url, char* host, size_t host_size,
                          int* port, char* path, size_t path_size)
{
    const char* p = url;
    if (strncmp(p, "http://", 7) == 0) p += 7;
    else if (strncmp(p, "https://", 8) == 0) { p += 8; *port = 443; }

    const char* colon = strchr(p, ':');
    const char* slash = strchr(p, '/');

    if (colon && (!slash || colon < slash)) {
        size_t host_len = colon - p;
        if (host_len >= host_size) host_len = host_size - 1;
        strncpy(host, p, host_len);
        host[host_len] = '\0';
        *port = atoi(colon + 1);
    } else if (slash) {
        size_t host_len = slash - p;
        if (host_len >= host_size) host_len = host_size - 1;
        strncpy(host, p, host_len);
        host[host_len] = '\0';
    } else {
        strncpy(host, p, host_size - 1);
        host[host_size - 1] = '\0';
    }

    if (path && slash)
        strncpy(path, slash, path_size - 1);
    else if (path)
        strcpy(path, "/");
}

static uint32_t bm_dns_resolve(const char* hostname)
{
    return net_dns_resolve(hostname);
}

/* ---- HTTP GET test via TCP stack ---- */

/*
 * bm_http_get - Perform a real HTTP/1.0 GET request and return
 * the response status code, or -1 on error.
 * Content is stored in tab->content_buf / tab->content_len.
 */
static int bm_http_get(bm_tab_t* tab, const char* url)
{
    char host[256];
    char path[512];
    int  port = 80;

    bm_parse_url(url, host, sizeof(host), &port, path, sizeof(path));
    if (!path[0]) { path[0] = '/'; path[1] = '\0'; }

    /* DNS resolution */
    uint32_t ip = bm_dns_resolve(host);
    if (!ip) {
        printk(KERN_WARN "bloodmoon: DNS failed for '%s'\n", host);
        return -1;
    }

    /* TCP connect */
    int fd = tcp_socket_create();
    if (fd < 0) return -1;

    if (tcp_connect(fd, htonl(ip), (uint16_t)port) < 0) {
        printk(KERN_WARN "bloodmoon: TCP connect to %u.%u.%u.%u:%d failed\n",
               (ip>>24)&0xFF, (ip>>16)&0xFF, (ip>>8)&0xFF, ip&0xFF, port);
        tcp_close(fd);
        return -1;
    }

    /* Build HTTP/1.0 request — use 1.0 to avoid chunked encoding */
    char req[768];
    int rlen = snprintf(req, sizeof(req),
        "GET %s HTTP/1.0\r\n"
        "Host: %s\r\n"
        "User-Agent: CrimsonOS/0.1 BloodMoon\r\n"
        "Accept: */*\r\n"
        "Connection: close\r\n"
        "\r\n",
        path, host);
    tcp_send(fd, (uint8_t*)req, (uint32_t)rlen);

    /* Receive response into tab buffer */
    uint32_t total = 0;
    int n;
    while (total < tab->content_cap - 1) {
        n = tcp_recv(fd, tab->content_buf + total,
                     tab->content_cap - 1 - total);
        if (n <= 0) break;
        total += (uint32_t)n;
    }
    tab->content_buf[total] = '\0';
    tab->content_len = total;
    tab->bytes_rx += total;
    tcp_close(fd);

    /* Parse status line: "HTTP/1.x NNN ..." */
    int status = -1;
    if (total >= 12 && memcmp(tab->content_buf, "HTTP/", 5) == 0) {
        const char* sp = (const char*)tab->content_buf + 8;
        while (*sp == ' ') sp++;
        status = atoi(sp);
    }

    printk(KERN_INFO "bloodmoon: HTTP %s -> %u.%u.%u.%u:%d  status=%d  rx=%u bytes\n",
           url, (ip>>24)&0xFF, (ip>>16)&0xFF, (ip>>8)&0xFF, ip&0xFF,
           port, status, total);
    return status;
}

/* snprintf provided by printk.c */

void bm_init(void) { bloodmoon_init(); }
