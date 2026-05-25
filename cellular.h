/* Copyright (c) 2026 SynChanCyberSecurity LLC. All Rights Reserved. */
#ifndef _CRIMSON_CELLULAR_H
#define _CRIMSON_CELLULAR_H

#include <crimson/types.h>
#include <crimson/spinlock.h>

/* LTE serving cell info */
typedef struct {
    uint32_t earfcn;
    uint32_t pci;
    uint32_t tac;
    uint32_t mcc, mnc;
    int32_t  rsrp, rsrq, snr;
    uint32_t band, bw;
} cell_lte_info_t;

/* 5G NR serving cell info */
typedef struct {
    uint32_t nrarfcn;
    uint32_t pci;
    int32_t  ss_rsrp, ss_rsrq, ss_sinr;
    uint32_t band;
} cell_nr_info_t;

typedef struct cell_driver cell_driver_t;

typedef struct cell_modem {
    uint32_t present;
    uint32_t sim_state;
    uint32_t pin_attempts;
    uint32_t reg_state;
    uint32_t rat;
    uint32_t roaming;
    char     imei[16];
    char     imsi[16];
    char     iccid[21];
    char     operator[32];
    int32_t  rssi, ber;
    cell_lte_info_t lte;
    cell_nr_info_t  nr;
    uint32_t data_active;
    uint64_t data_rx_bytes, data_tx_bytes;
    uint32_t gnss_active;
    double   gnss_lat, gnss_lon, gnss_alt;
    float    gnss_accuracy;
    spinlock_t lock;
    cell_driver_t* driver;
    void*    driver_priv;
} cell_modem_t;

struct cell_driver {
    const char* name;
    int  (*probe)(cell_modem_t*);
    void (*remove)(cell_modem_t*);
    int  (*at_command)(cell_modem_t*, const char*, char*, uint32_t, uint32_t);
    int  (*send_sms_pdu)(cell_modem_t*, const uint8_t*, uint32_t);
    int  (*read_sms_pdu)(cell_modem_t*, uint32_t, uint8_t*, uint32_t*);
    int  (*delete_sms)(cell_modem_t*, uint32_t);
    int  (*setup_data)(cell_modem_t*, const void*);
    int  (*start_data)(cell_modem_t*);
    int  (*stop_data)(cell_modem_t*);
    int  (*start_call)(cell_modem_t*, const char*);
    int  (*answer_call)(cell_modem_t*);
    int  (*hangup_call)(cell_modem_t*);
    int  (*send_dtmf)(cell_modem_t*, char);
    int  (*send_ussd)(cell_modem_t*, const char*);
    int  (*get_signal)(cell_modem_t*);
    int  (*get_registration)(cell_modem_t*);
    int  (*get_cell_info)(cell_modem_t*);
    int  (*enter_pin)(cell_modem_t*, const char*);
    int  (*set_rat)(cell_modem_t*, uint32_t);
    int  (*scan_operators)(cell_modem_t*);
    int  (*gnss_start)(cell_modem_t*);
    int  (*gnss_stop)(cell_modem_t*);
    int  (*gnss_read)(cell_modem_t*, double*, double*, double*, float*);
    void (*power_on)(cell_modem_t*);
    void (*power_off)(cell_modem_t*);
    void (*reset)(cell_modem_t*);
};

void cellular_init(void);
cell_modem_t* cellular_probe(cell_driver_t* drv);
int cellular_enter_pin(const char* pin);
int cellular_get_signal(void);
int cellular_send_sms(const char* dest, const char* text);
int cellular_start_call(const char* number);
int cellular_answer_call(void);
int cellular_hangup(void);
int cellular_send_ussd(const char* code);
int cellular_setup_data(const char* apn, const char* user, const char* pass);
int cellular_start_data(void);
int cellular_stop_data(void);
int cellular_gnss_start(void);
int cellular_gnss_read(double* lat, double* lon, double* alt, float* acc);
void cellular_get_status(void);
void cellular_power_on(void);
void cellular_power_off(void);
void cellular_reset(void);

#endif
