#ifndef _CRIMSON_CELLULAR_H
#define _CRIMSON_CELLULAR_H

#include <crimson/types.h>

typedef struct {
    uint32_t present;
    uint32_t sim_state;
    uint32_t reg_state;
    uint32_t rat;
    char imei[16];
    char imsi[16];
    char iccid[21];
    char operator[32];
    int32_t rssi;
    uint32_t data_active;
    uint32_t gnss_active;
} cell_modem_t;

void cellular_init(void);
cell_modem_t* cellular_probe(struct cell_driver* drv);
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
