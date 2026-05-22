#ifndef _CRIMSON_POWER_H
#define _CRIMSON_POWER_H

#include <crimson/types.h>

#define GOV_PERFORMANCE     0
#define GOV_POWERSAVE       1
#define GOV_ONDEMAND        2
#define GOV_CONSERVATIVE    3
#define GOV_INTERACTIVE     4

#define WAKE_SRC_RTC        (1<<0)
#define WAKE_SRC_USB        (1<<1)
#define WAKE_SRC_MODEM      (1<<2)
#define WAKE_SRC_WLAN       (1<<3)
#define WAKE_SRC_BLUETOOTH  (1<<4)
#define WAKE_SRC_GPIO       (1<<5)
#define WAKE_SRC_TOUCH      (1<<6)

typedef struct {
    uint32_t present;
    uint32_t voltage_mv;
    uint32_t current_ma;
    uint32_t capacity_percent;
    uint32_t temp_c;
    uint32_t status;
    uint32_t health;
    uint32_t charger_present;
    uint32_t charger_type;
} battery_state_t;

void pm_init(void);
void pm_request_suspend(void);
void pm_request_resume(uint32_t wake_src);
void pm_set_governor(uint32_t gov);
void pm_boostpulse(void);
void pm_get_battery_status(battery_state_t* out);
void pm_set_display_state(uint32_t on);
void pm_power_domain(const char* name, uint32_t enable);
void pm_get_stats(void);

#endif
