/*
 * Crimson OS - Power Management Subsystem
 *
 * Advanced power management for mobile devices:
 *   - DVFS (Dynamic Voltage and Frequency Scaling) for CPU/GPU
 *   - CPU idle states (WFI, power gating, cluster retention)
 *   - Thermal management with throttling
 *   - Display panel power control (self-refresh, blanking)
 *   - Peripheral power gating (USB, WiFi, Bluetooth, GPS)
 *   - Battery fuel gauge (MAX17048, BQ27441)
 *   - Charger detection (BC1.2, USB-PD, QC3.0)
 *   - Suspend-to-RAM and hibernate
 *   - Wake-on-LAN, wake-on-RTC, wake-on-modem
 *
 * Governor policies:
 *   - performance: max frequency always
 *   - powersave: min frequency always
 *   - ondemand: scale up on load, scale down after idle
 *   - conservative: gradual scaling
 *   - interactive: touch-boost, scale down with timers (default for mobile)
 */

#include <crimson/types.h>
#include <crimson/printk.h>
#include <crimson/interrupt.h>
#include <crimson/memory.h>
#include <crimson/spinlock.h>
#include <crimson/string.h>
#include <crimson/timer.h>
#include <crimson/scheduler.h>
#include <crimson/gpio.h>

/* ---- PMIC (Power Management IC) ---- */
#define PMIC_BASE               0xFE00E000   /* RPi4 PMIC via I2C */
#define PMIC_I2C_ADDR           0x40

/* CPU frequency table (Cortex-A72) */
typedef struct {
    uint32_t freq_khz;
    uint32_t voltage_mv;
    uint32_t power_mw;      /* Estimated power consumption */
} opp_t;   /* Operating Performance Point */

static const opp_t cpu_opps[] = {
    {  600000,  825,  350 },
    {  800000,  875,  550 },
    { 1000000,  925,  800 },
    {1200000, 1000, 1100 },
    {1500000, 1100, 1600 },
    {1800000, 1200, 2300 },
    {2100000, 1325, 3200 },
};
#define NUM_CPU_OPPS    (sizeof(cpu_opps) / sizeof(cpu_opps[0]))

/* CPU governor types */
#define GOV_PERFORMANCE     0
#define GOV_POWERSAVE       1
#define GOV_ONDEMAND        2
#define GOV_CONSERVATIVE    3
#define GOV_INTERACTIVE     4
#define GOV_NUM             5

static const char* gov_names[] = {
    "performance", "powersave", "ondemand", "conservative", "interactive"
};

/* Governor tunables for "interactive" (default mobile governor) */
typedef struct {
    uint32_t hispeed_freq;       /* Jump to this on touch */
    uint32_t go_hispeed_load;    /* Load % to trigger hispeed */
    uint32_t min_sample_time;    /* Min time at freq before scaling down */
    uint32_t timer_rate;         /* Load sampling rate (ms) */
    uint32_t above_hispeed_delay;
    uint32_t target_loads;       /* Target CPU load % */
    uint32_t boost;              /* Touch boost active */
    uint32_t boostpulse_duration;
} interactive_tunables_t;

static interactive_tunables_t interactive_defaults = {
    .hispeed_freq       = 1500000,
    .go_hispeed_load    = 85,
    .min_sample_time    = 40,
    .timer_rate         = 20,
    .above_hispeed_delay = 20000,
    .target_loads       = 80,
    .boost              = 0,
    .boostpulse_duration = 100000,   /* 100ms touch boost */
};

/* Power domain state */
typedef struct {
    const char* name;
    uint32_t enabled;
    uint32_t can_sleep;
    uint32_t wakeup_source;
    uint32_t refcount;
} power_domain_t;

/* Thermal zone */
typedef struct {
    const char* name;
    int32_t  temperature;       /* Millidegrees Celsius */
    int32_t  trip_points[4];    /* Passive, critical, hot, emergency */
    uint32_t throttle_freq;     /* Throttled to this freq */
    int32_t  last_temp;
    int      (*get_temp)(void);
} thermal_zone_t;

/* Battery / charger */
typedef struct {
    uint32_t present;
    uint32_t voltage_mv;        /* Battery voltage */
    uint32_t current_ma;        /* Charging (+) / discharging (-) current */
    uint32_t capacity_percent;  /* State of charge */
    uint32_t capacity_mah;      /* Design capacity */
    uint32_t remaining_mah;
    uint32_t temp_c;            /* Battery temperature */
    uint32_t cycle_count;
    uint32_t health;            /* GOOD, OVERHEAT, DEAD, OVERVOLTAGE */
    uint32_t status;            /* CHARGING, DISCHARGING, FULL, NOT_PRESENT */
    uint32_t time_to_full_min;
    uint32_t time_to_empty_min;

    /* Charger */
    uint32_t charger_present;
    uint32_t charger_type;      /* NONE, USB, AC, WIRELESS */
    uint32_t charger_voltage;   /* mV */
    uint32_t charger_current;   /* mA limit */

    /* Fuel gauge IC */
    int (*read_voltage)(void);
    int (*read_current)(void);
    int (*read_capacity)(void);
    int (*read_temp)(void);
} battery_state_t;

#define BATTERY_HEALTH_GOOD         0
#define BATTERY_HEALTH_OVERHEAT     1
#define BATTERY_HEALTH_DEAD         2
#define BATTERY_HEALTH_OVERVOLTAGE  3
#define BATTERY_HEALTH_COLD         4
#define BATTERY_HEALTH_UNKNOWN      5

#define BATTERY_STATUS_CHARGING     0
#define BATTERY_STATUS_DISCHARGING  1
#define BATTERY_STATUS_FULL         2
#define BATTERY_STATUS_NOT_CHARGING 3
#define BATTERY_STATUS_UNKNOWN      4

/* System power state */
#define POWER_STATE_ON              0
#define POWER_STATE_SUSPEND         1   /* Suspend-to-RAM */
#define POWER_STATE_HIBERNATE       2   /* Suspend-to-disk */
#define POWER_STATE_OFF             3

/* Wake sources */
#define WAKE_SRC_RTC                (1 << 0)
#define WAKE_SRC_USB                (1 << 1)
#define WAKE_SRC_MODEM              (1 << 2)
#define WAKE_SRC_WLAN               (1 << 3)
#define WAKE_SRC_BLUETOOTH          (1 << 4)
#define WAKE_SRC_GPIO               (1 << 5)
#define WAKE_SRC_TOUCH              (1 << 6)
#define WAKE_SRC_HEADSET            (1 << 7)

/* Power management state */
typedef struct {
    /* CPU DVFS */
    uint32_t cur_freq;
    uint32_t target_freq;
    uint32_t min_freq;
    uint32_t max_freq;
    uint32_t gov_type;
    interactive_tunables_t gov_tunables;
    uint32_t cpu_load[4];       /* Per-core load average */

    /* Idle */
    uint64_t idle_time_us[4];
    uint64_t busy_time_us[4];

    /* Governor timer */
    uint32_t gov_timer;

    /* Thermal */
    thermal_zone_t* thermals[8];
    uint32_t num_thermals;
    uint32_t thermal_throttled;

    /* Battery */
    battery_state_t battery;

    /* Power domains */
    power_domain_t domains[16];
    uint32_t num_domains;

    /* System state */
    uint32_t system_state;
    uint32_t wake_sources;
    uint32_t display_on;
    uint32_t suspend_in_progress;

    /* Display brightness saved for resume */
    uint32_t saved_brightness;

    /* Sleep statistics */
    uint64_t total_suspend_time;
    uint64_t total_idle_time;

    spinlock_t lock;
} pm_state_t;

static pm_state_t g_pm;

/* Forward declarations */
static void pm_gov_timer(void* arg);
static void pm_interactive_governor(void);
static void pm_set_cpu_freq(uint32_t freq);
static int  pm_thermal_read_cpu(void);
static int  pm_thermal_read_battery(void);
static void pm_thermal_check(void);
static int  pm_battery_read_max17048(void);
static void pm_enter_suspend(void);

/* ---- Public API ---- */

void pm_init(void)
{
    memset(&g_pm, 0, sizeof(g_pm));
    spinlock_init(&g_pm.lock);

    /* Default CPU at middle OPP */
    g_pm.min_freq = cpu_opps[0].freq_khz;
    g_pm.max_freq = cpu_opps[NUM_CPU_OPPS - 1].freq_khz;
    g_pm.cur_freq = cpu_opps[2].freq_khz;
    g_pm.target_freq = g_pm.cur_freq;
    g_pm.gov_type = GOV_INTERACTIVE;
    g_pm.gov_tunables = interactive_defaults;
    g_pm.display_on = 1;
    g_pm.system_state = POWER_STATE_ON;

    /* Register thermal zones */
    static thermal_zone_t tz_cpu = {
        .name = "CPU", .trip_points = {70000, 85000, 95000, 105000},
        .get_temp = pm_thermal_read_cpu,
    };
    static thermal_zone_t tz_batt = {
        .name = "Battery", .trip_points = {40000, 45000, 50000, 60000},
        .get_temp = pm_thermal_read_battery,
    };
    g_pm.thermals[0] = &tz_cpu;
    g_pm.thermals[1] = &tz_batt;
    g_pm.num_thermals = 2;

    /* Register power domains */
    const char* domain_names[] = {
        "USB", "WiFi", "Bluetooth", "Display", "Camera",
        "Audio", "GPU", "Cellular", "GPS", "NFC",
        "SD", "Sensors", "Vibrator", "LED", "Modem"
    };
    for (int i = 0; i < 15 && i < 16; i++) {
        g_pm.domains[i].name = domain_names[i];
        g_pm.domains[i].enabled = 1;
        g_pm.domains[i].can_sleep = 1;
    }
    g_pm.num_domains = 15;

    /* Set initial CPU frequency */
    pm_set_cpu_freq(g_pm.cur_freq);

    /* Start governor timer (50ms for interactive) */
    g_pm.gov_timer = timer_create(pm_gov_timer, NULL);
    timer_set_periodic(g_pm.gov_timer, interactive_defaults.timer_rate);

    printk(KERN_INFO "pm: power management initialised\n");
    printk(KERN_INFO "pm: governor=interactive, %d OPPs, %d thermal zones\n",
           NUM_CPU_OPPS, g_pm.num_thermals);
}

/*
 * pm_request_suspend - Request system suspend
 */
void pm_request_suspend(void)
{
    spin_lock(&g_pm.lock);
    if (g_pm.suspend_in_progress) {
        spin_unlock(&g_pm.lock);
        return;
    }
    g_pm.suspend_in_progress = 1;
    spin_unlock(&g_pm.lock);

    printk(KERN_INFO "pm: suspend requested\n");

    /* Notify subsystems */
    /* wifi_set_power_save(..., max) */
    /* cellular_set_psm(...) */

    /* Save display brightness */
    extern uint32_t display_get_brightness(void);
    g_pm.saved_brightness = 0;   /* Stub */

    /* Turn off display */
    extern void display_set_brightness(uint32_t);
    display_set_brightness(0);
    g_pm.display_on = 0;

    /* Power down non-wake domains */
    for (uint32_t i = 0; i < g_pm.num_domains; i++) {
        if (!g_pm.domains[i].wakeup_source && g_pm.domains[i].can_sleep) {
            g_pm.domains[i].enabled = 0;
            printk(KERN_DEBUG "pm: power down %s\n", g_pm.domains[i].name);
        }
    }

    /* Set minimum CPU frequency */
    pm_set_cpu_freq(g_pm.min_freq);

    /* Enter suspend */
    pm_enter_suspend();
}

/*
 * pm_request_resume - Resume from suspend
 */
void pm_request_resume(uint32_t wake_src)
{
    g_pm.system_state = POWER_STATE_ON;
    g_pm.suspend_in_progress = 0;

    printk(KERN_INFO "pm: resume from %s\n",
           wake_src == WAKE_SRC_RTC ? "RTC" :
           wake_src == WAKE_SRC_MODEM ? "modem" :
           wake_src == WAKE_SRC_TOUCH ? "touch" :
           wake_src == WAKE_SRC_USB ? "USB" : "unknown");

    /* Restore power domains */
    for (uint32_t i = 0; i < g_pm.num_domains; i++) {
        g_pm.domains[i].enabled = 1;
    }

    /* Restore display */
    extern void display_set_brightness(uint32_t);
    display_set_brightness(g_pm.saved_brightness);
    g_pm.display_on = 1;

    /* Restore CPU frequency */
    pm_set_cpu_freq(g_pm.target_freq);

    /* Enable touch */
    extern void touch_init(void);
    touch_init();
}

/*
 * pm_set_governor - Change CPU governor
 */
void pm_set_governor(uint32_t gov)
{
    if (gov >= GOV_NUM) return;
    g_pm.gov_type = gov;
    printk(KERN_INFO "pm: governor -> %s\n", gov_names[gov]);

    if (gov == GOV_PERFORMANCE)
        pm_set_cpu_freq(g_pm.max_freq);
    else if (gov == GOV_POWERSAVE)
        pm_set_cpu_freq(g_pm.min_freq);
}

/*
 * pm_boostpulse - Touch boost (called from touch driver)
 */
void pm_boostpulse(void)
{
    if (g_pm.gov_type != GOV_INTERACTIVE) return;

    g_pm.gov_tunables.boost = 1;
    g_pm.target_freq = g_pm.gov_tunables.hispeed_freq;
    pm_set_cpu_freq(g_pm.target_freq);

    /* Schedule boost end */
    /* timer_set_oneshot(..., g_pm.gov_tunables.boostpulse_duration) */
}

/*
 * pm_get_battery_status - Return battery information
 */
void pm_get_battery_status(battery_state_t* out)
{
    if (out) memcpy(out, &g_pm.battery, sizeof(battery_state_t));
}

/*
 * pm_set_display_state - Turn display on/off
 */
void pm_set_display_state(uint32_t on)
{
    if (on == g_pm.display_on) return;
    g_pm.display_on = on;

    if (on) {
        extern void display_set_brightness(uint32_t level);
        display_set_brightness(200);
        g_pm.gov_tunables.boost = 1;
    } else {
        extern void display_set_brightness(uint32_t level);
        display_set_brightness(0);
        /* Lower CPU freq when display off */
        if (g_pm.gov_type == GOV_INTERACTIVE)
            pm_set_cpu_freq(g_pm.min_freq);
    }
}

/*
 * pm_power_domain - Enable/disable a power domain
 */
void pm_power_domain(const char* name, uint32_t enable)
{
    for (uint32_t i = 0; i < g_pm.num_domains; i++) {
        if (strcmp(g_pm.domains[i].name, name) == 0) {
            g_pm.domains[i].enabled = enable;
            printk(KERN_DEBUG "pm: %s %s\n", name, enable ? "on" : "off");
            return;
        }
    }
}

/*
 * pm_get_stats - Print power management statistics
 */
void pm_get_stats(void)
{
    printk("\n=== Power Management Stats ===\n");
    printk("CPU: %d MHz (gov: %s)\n", g_pm.cur_freq / 1000, gov_names[g_pm.gov_type]);
    printk("Thermal throttled: %s\n", g_pm.thermal_throttled ? "YES" : "no");
    printk("Display: %s\n", g_pm.display_on ? "on" : "off");
    printk("Battery: %d%%, %d mV, %s\n",
           g_pm.battery.capacity_percent,
           g_pm.battery.voltage_mv,
           g_pm.battery.status == BATTERY_STATUS_CHARGING ? "charging" :
           g_pm.battery.status == BATTERY_STATUS_FULL ? "full" : "discharging");
    printk("Power domains:\n");
    for (uint32_t i = 0; i < g_pm.num_domains; i++) {
        printk("  %s: %s\n", g_pm.domains[i].name,
               g_pm.domains[i].enabled ? "on" : "off");
    }
    printk("Total suspend time: %lu ms\n", g_pm.total_suspend_time);
    printk("==============================\n\n");
}

/* ---- Internal ---- */

static void pm_gov_timer(void* arg)
{
    (void)arg;

    /* Read CPU load */
    /* Simplified: use scheduler load */
    extern uint32_t scheduler_get_load(void);
    uint32_t load = scheduler_get_load();
    g_pm.cpu_load[0] = load;

    /* Run governor */
    switch (g_pm.gov_type) {
        case GOV_ONDEMAND:
            if (load > 80 && g_pm.cur_freq < g_pm.max_freq) {
                /* Scale up */
                for (uint32_t i = 0; i < NUM_CPU_OPPS; i++) {
                    if (cpu_opps[i].freq_khz > g_pm.cur_freq) {
                        pm_set_cpu_freq(cpu_opps[i].freq_khz);
                        break;
                    }
                }
            } else if (load < 20 && g_pm.cur_freq > g_pm.min_freq) {
                /* Scale down */
                for (int i = NUM_CPU_OPPS - 1; i >= 0; i--) {
                    if (cpu_opps[i].freq_khz < g_pm.cur_freq) {
                        pm_set_cpu_freq(cpu_opps[i].freq_khz);
                        break;
                    }
                }
            }
            break;

        case GOV_INTERACTIVE:
            pm_interactive_governor();
            break;

        default:
            break;
    }

    /* Thermal management */
    pm_thermal_check();

    /* Battery monitoring */
    if (g_pm.battery.present) {
        pm_battery_read_max17048();
    }
}

static void pm_interactive_governor(void)
{
    interactive_tunables_t* t = &g_pm.gov_tunables;
    uint32_t load = g_pm.cpu_load[0];

    /* Handle touch boost */
    if (t->boost) {
        t->boost = 0;   /* Clear after one tick */
        pm_set_cpu_freq(t->hispeed_freq);
        return;
    }

    if (load >= t->go_hispeed_load) {
        /* High load: go to hispeed or above */
        if (g_pm.cur_freq < t->hispeed_freq) {
            pm_set_cpu_freq(t->hispeed_freq);
        } else {
            /* Above hispeed: scale proportional to load */
            uint32_t freq = g_pm.cur_freq;
            freq = (freq * load) / t->target_loads;
            if (freq > g_pm.max_freq) freq = g_pm.max_freq;
            pm_set_cpu_freq(freq);
        }
    } else {
        /* Low load: scale down */
        if (g_pm.cur_freq > g_pm.min_freq) {
            uint32_t freq = g_pm.cur_freq;
            freq = (freq * load) / t->target_loads;
            if (freq < g_pm.min_freq) freq = g_pm.min_freq;
            pm_set_cpu_freq(freq);
        }
    }
}

static void pm_set_cpu_freq(uint32_t freq)
{
    /* Clamp to thermal limits */
    if (g_pm.thermal_throttled && freq > g_pm.max_freq)
        freq = g_pm.max_freq;

    /* Find nearest OPP */
    uint32_t best = cpu_opps[0].freq_khz;
    for (uint32_t i = 0; i < NUM_CPU_OPPS; i++) {
        if (cpu_opps[i].freq_khz <= freq)
            best = cpu_opps[i].freq_khz;
    }

    if (best == g_pm.cur_freq) return;

    /* Set voltage first (if increasing) */
    uint32_t new_voltage = 0;
    for (uint32_t i = 0; i < NUM_CPU_OPPS; i++) {
        if (cpu_opps[i].freq_khz == best) {
            new_voltage = cpu_opps[i].voltage_mv;
            break;
        }
    }

    /* PMIC voltage write would go here */
    (void)new_voltage;

    /* Update clock */
    /* Clock manager register write would go here */
    g_pm.cur_freq = best;

    /* printk(KERN_DEBUG "pm: CPU -> %d MHz @ %d mV\n", best / 1000, new_voltage); */
}

static int pm_thermal_read_cpu(void)
{
    /* Read on-die temperature sensor */
    /* ARM64: mrs x0, cntpct_el0 is not temp, use platform sensor */
    /* Simplified: return simulated value */
    return 45000;   /* 45C */
}

static int pm_thermal_read_battery(void)
{
    return g_pm.battery.temp_c * 1000;
}

static void pm_thermal_check(void)
{
    for (uint32_t i = 0; i < g_pm.num_thermals; i++) {
        thermal_zone_t* tz = g_pm.thermals[i];
        if (!tz || !tz->get_temp) continue;

        int32_t temp = tz->get_temp();
        tz->temperature = temp;

        /* Check trip points */
        if (temp >= tz->trip_points[3]) {
            /* Emergency: emergency shutdown */
            printk(KERN_CRIT "pm: %s at %dC - EMERGENCY SHUTDOWN!\n", tz->name, temp / 1000);
            /* Trigger emergency shutdown */
        } else if (temp >= tz->trip_points[2]) {
            /* Hot: max throttling */
            g_pm.thermal_throttled = 1;
            g_pm.max_freq = cpu_opps[0].freq_khz;   /* Min freq */
            pm_set_cpu_freq(g_pm.max_freq);
            printk(KERN_WARN "pm: %s at %dC - max throttle\n", tz->name, temp / 1000);
        } else if (temp >= tz->trip_points[1]) {
            /* Critical: start throttling */
            g_pm.thermal_throttled = 1;
            g_pm.max_freq = cpu_opps[2].freq_khz;   /* Medium freq */
            if (g_pm.cur_freq > g_pm.max_freq)
                pm_set_cpu_freq(g_pm.max_freq);
            printk(KERN_WARN "pm: %s at %dC - throttling\n", tz->name, temp / 1000);
        } else if (temp < tz->trip_points[0]) {
            /* Below passive: remove throttling */
            if (g_pm.thermal_throttled) {
                g_pm.thermal_throttled = 0;
                g_pm.max_freq = cpu_opps[NUM_CPU_OPPS - 1].freq_khz;
                printk(KERN_INFO "pm: %s cooled to %dC - throttle removed\n",
                       tz->name, temp / 1000);
            }
        }
    }
}

static int pm_battery_read_max17048(void)
{
    /* MAX17048 fuel gauge over I2C */
    /* Read VCELL, SOC, CRATE registers */
    /* Simplified: simulate values */
    g_pm.battery.present = 1;
    g_pm.battery.voltage_mv = 3800 + (g_pm.battery.capacity_percent * 5);
    g_pm.battery.capacity_percent = 75;
    g_pm.battery.current_ma = g_pm.battery.charger_present ? 500 : -200;
    g_pm.battery.temp_c = 30;
    g_pm.battery.status = g_pm.battery.charger_present ?
                          BATTERY_STATUS_CHARGING : BATTERY_STATUS_DISCHARGING;
    g_pm.battery.health = BATTERY_HEALTH_GOOD;
    g_pm.battery.time_to_empty_min = (g_pm.battery.remaining_mah * 60) / 200;
    return 0;
}

static void pm_enter_suspend(void)
{
    uint64_t suspend_start = timer_get_uptime_ms();

    printk(KERN_INFO "pm: entering suspend-to-RAM...\n");

    /* Save CPU context */
    /* Set wake sources */
    /* Enable GPIO wake IRQs */
    /* Program RTC alarm if needed */

    /* WFI - wait for interrupt */
    while (g_pm.system_state == POWER_STATE_SUSPEND) {
        __asm__ volatile("wfi");
    }

    uint64_t suspend_end = timer_get_uptime_ms();
    g_pm.total_suspend_time += (suspend_end - suspend_start);

    printk(KERN_INFO "pm: resumed after %lu ms\n", suspend_end - suspend_start);
}
