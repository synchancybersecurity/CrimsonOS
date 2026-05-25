/* Copyright (c) 2026 SynChanCyberSecurity LLC. All Rights Reserved. */
#include <crimson/spi.h>
#include <crimson/uart.h>
#include <crimson/memory.h>

#define SPI_MAX_BUS 4
static spi_master_t *g_masters[SPI_MAX_BUS];
static spi_device_t *g_user_devices[16];
static int g_next_fd = 0;

int spi_register_master(spi_master_t *master) {
    if (master->bus_id >= SPI_MAX_BUS) return -1;
    if (g_masters[master->bus_id]) return -1;
    g_masters[master->bus_id] = master;
    uart_puts("[spi] registered master\n");
    return 0;
}

spi_device_t *spi_alloc_device(uint8_t bus, uint8_t cs) {
    if (bus >= SPI_MAX_BUS || !g_masters[bus]) return NULL;
    spi_master_t *m = g_masters[bus];
    if (cs >= m->num_chipselect) return NULL;
    spi_device_t *dev = kmalloc(sizeof(spi_device_t));
    if (!dev) return NULL;
    dev->master = m; dev->cs = cs; dev->mode = 0;
    dev->max_speed_hz = m->max_speed_hz;
    m->setup(m, cs, dev->mode, dev->max_speed_hz);
    return dev;
}

void spi_free_device(spi_device_t *dev) { if (dev) kfree(dev); }

int spi_sync_transfer(spi_device_t *dev, const spi_transfer_t *xfers, size_t n) {
    if (!dev || !dev->master) return -1;
    if (dev->master->transfer_multi)
        return dev->master->transfer_multi(dev->master, dev->cs, xfers, n);
    for (size_t i = 0; i < n; i++) {
        int rc = dev->master->transfer(dev->master, dev->cs, &xfers[i]);
        if (rc < 0) return rc;
    }
    return 0;
}

int sys_spi_open(uint8_t bus, uint8_t cs, uint32_t speed_hz) {
    spi_device_t *dev = spi_alloc_device(bus, cs);
    if (!dev) return -1;
    if (speed_hz && speed_hz < dev->max_speed_hz) dev->max_speed_hz = speed_hz;
    int fd = g_next_fd++;
    if (fd >= 16) { spi_free_device(dev); return -1; }
    g_user_devices[fd] = dev;
    return fd;
}

int sys_spi_close(int fd) {
    if (fd < 0 || fd >= 16) return -1;
    if (g_user_devices[fd]) { spi_free_device(g_user_devices[fd]); g_user_devices[fd] = NULL; }
    return 0;
}

int sys_spi_transfer(int fd, const void *tx, void *rx, size_t len) {
    if (fd < 0 || fd >= 16 || !g_user_devices[fd]) return -1;
    spi_transfer_t xfer = { .tx_buf = (const uint8_t *)tx, .rx_buf = (uint8_t *)rx,
                            .len = len, .speed_hz = 0, .cs_change = 1 };
    return spi_sync_transfer(g_user_devices[fd], &xfer, 1);
}
