/* Copyright (c) 2026 SynChanCyberSecurity LLC. All Rights Reserved. */
/*
 * Crimson OS - BloodMoon Browser Header
 * Multi-Network Browser Engine (ClearNet/Tor/I2P/Freenet)
 */

#ifndef _CRIMSON_BLOODMOON_H
#define _CRIMSON_BLOODMOON_H

#include <crimson/types.h>
#include <crimson/net.h>

/* BloodMoon version */
#define BLOODMOON_VERSION_MAJOR 1
#define BLOODMOON_VERSION_MINOR 0
#define BLOODMOON_VERSION_PATCH 0

/* Network routing types */
#define BM_NET_CLEAR        0
#define BM_NET_TOR          1
#define BM_NET_I2P          2
#define BM_NET_FREENET      3
#define BM_NET_MAX          4

/* Tab states */
#define BM_TAB_EMPTY        0
#define BM_TAB_LOADING      1
#define BM_TAB_READY        2
#define BM_TAB_ERROR        3
#define BM_TAB_SECURE       4

/* Tab limits */
#define BM_MAX_TABS         32
#define BM_MAX_URL_LEN      2048
#define BM_MAX_TITLE_LEN    256
#define BM_MAX_HISTORY      128
#define BM_MAX_COOKIES      256
#define BM_MAX_BOOKMARKS    512

/* Content types */
#define BM_CT_HTML          1
#define BM_CT_TEXT          2
#define BM_CT_IMAGE         3
#define BM_CT_JSON          4
#define BM_CT_BINARY        5
#define BM_CT_CSS           6
#define BM_CT_JS            7

/* Security indicators */
#define BM_SEC_NONE         0
#define BM_SEC_HTTP         1
#define BM_SEC_HTTPS        2
#define BM_SEC_EV_HTTPS     3
#define BM_SEC_ONION        4
#define BM_SEC_I2P          5
#define BM_SEC_FREENET      6

typedef struct bm_cookie {
    char    domain[256];
    char    name[128];
    char    value[1024];
    char    path[256];
    uint64_t expires;
    uint32_t secure;
    uint32_t httponly;
} bm_cookie_t;

typedef struct bm_history_entry {
    char        url[BM_MAX_URL_LEN];
    char        title[BM_MAX_TITLE_LEN];
    uint64_t    timestamp;
    uint32_t    network;
} bm_history_entry_t;

typedef struct bm_bookmark {
    char        url[BM_MAX_URL_LEN];
    char        title[BM_MAX_TITLE_LEN];
    char        folder[64];
    uint32_t    network;
    uint64_t    created;
} bm_bookmark_t;

typedef struct bm_tab {
    uint32_t    id;
    uint32_t    state;
    uint32_t    network;
    char        url[BM_MAX_URL_LEN];
    char        title[BM_MAX_TITLE_LEN];
    char        content[32768];
    uint32_t    content_len;
    uint32_t    content_type;
    uint32_t    security_level;
    int         socket_fd;
    uint32_t    load_progress;
    uint64_t    load_start;
    uint64_t    load_end;
    uint32_t    scroll_x;
    uint32_t    scroll_y;
    uint32_t    history_pos;
    bm_history_entry_t history[BM_MAX_HISTORY];
    uint32_t    active;
} bm_tab_t;

typedef struct bm_session {
    bm_tab_t        tabs[BM_MAX_TABS];
    uint32_t        tab_count;
    uint32_t        active_tab;
    bm_cookie_t     cookies[BM_MAX_COOKIES];
    uint32_t        cookie_count;
    bm_bookmark_t   bookmarks[BM_MAX_BOOKMARKS];
    uint32_t        bookmark_count;
    uint32_t        tor_enabled;
    uint32_t        i2p_enabled;
    uint32_t        freenet_enabled;
    uint32_t        js_enabled;
    uint32_t        cookies_enabled;
    uint32_t        private_mode;
    char            user_agent[256];
    uint64_t        bytes_clear;
    uint64_t        bytes_tor;
    uint64_t        bytes_i2p;
    uint64_t        bytes_freenet;
} bm_session_t;

/* Session lifecycle */
void bm_init(void);
void bm_shutdown(void);
bm_session_t* bm_get_session(void);

/* Tab management */
int bm_tab_open(uint32_t network, const char* url);
void bm_tab_close(int tab_id);
void bm_tab_activate(int tab_id);
int bm_tab_navigate(int tab_id, const char* url);
int bm_tab_reload(int tab_id);
void bm_tab_back(int tab_id);
void bm_tab_forward(int tab_id);
bm_tab_t* bm_tab_get(int tab_id);

/* Network routing */
void bm_tab_set_network(int tab_id, uint32_t network);
const char* bm_network_name(uint32_t network);
uint32_t bm_network_from_url(const char* url);

/* Content handling */
int bm_fetch_http(bm_tab_t* tab, const char* url);
int bm_fetch_tor(bm_tab_t* tab, const char* onion_url);
int bm_fetch_i2p(bm_tab_t* tab, const char* i2p_url);
int bm_fetch_freenet(bm_tab_t* tab, const char* freenet_key);
int bm_render_html(bm_tab_t* tab);
void bm_clear_content(bm_tab_t* tab);

/* HTML parsing & rendering */
void bm_parse_html(bm_tab_t* tab);
void bm_parse_css(bm_tab_t* tab);
void bm_layout_page(bm_tab_t* tab);
void bm_render_to_surface(bm_tab_t* tab);

/* Bookmarks */
int bm_bookmark_add(const char* url, const char* title, const char* folder, uint32_t network);
void bm_bookmark_remove(int idx);
void bm_bookmark_list(void);

/* History */
void bm_history_add(bm_tab_t* tab, const char* url, const char* title);
void bm_history_clear(void);
void bm_history_search(const char* query);

/* Privacy */
void bm_set_private_mode(uint32_t enabled);
void bm_clear_cookies(void);
void bm_clear_cache(void);
void bm_set_user_agent(const char* ua);

/* Network statistics */
void bm_print_stats(void);
void bm_reset_stats(void);

/* URL utilities */
int bm_url_parse(const char* url, char* scheme, char* host, char* path, uint16_t* port);
int bm_is_onion(const char* url);
int bm_is_i2p(const char* url);
int bm_is_freenet(const char* url);
void bm_normalize_url(const char* input, char* output, size_t out_len);

/* Tor/I2P/Freenet client management */
int bm_tor_start(void);
void bm_tor_stop(void);
int bm_i2p_start(void);
void bm_i2p_stop(void);
int bm_freenet_start(void);
void bm_freenet_stop(void);
int bm_tor_status(void);
int bm_i2p_status(void);
int bm_freenet_status(void);

/* Security */
int bm_check_cert(const char* host, uint32_t port);
void bm_show_security_info(int tab_id);

#endif /* _CRIMSON_BLOODMOON_H */
