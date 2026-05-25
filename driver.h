/* Copyright (c) 2026 SynChanCyberSecurity LLC. All Rights Reserved. */
#ifndef _CRIMSON_DRIVER_H
#define _CRIMSON_DRIVER_H

#include <crimson/types.h>

#define DRIVER_NAME_MAX     64

typedef enum {
    DRIVER_BUS_PLATFORM = 0,
    DRIVER_BUS_PCI,
    DRIVER_BUS_USB,
    DRIVER_BUS_MMIO,
} driver_bus_t;

typedef struct driver {
    char name[DRIVER_NAME_MAX];
    driver_bus_t bus;
    int (*probe)(void);
    int (*remove)(void);
    void (*shutdown)(void);
    struct driver* next;
} driver_t;

void driver_register(driver_t* drv);
void driver_register_all(void);
int driver_probe_all(void);
void driver_shutdown_all(void);

#endif
