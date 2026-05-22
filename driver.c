/*
 * Crimson OS - Driver Registration Framework
 * 
 * Manages device driver registration, probing, and lifecycle.
 */

#include <crimson/types.h>
#include <crimson/driver.h>
#include <crimson/printk.h>

static driver_t* driver_list = NULL;

void driver_register(driver_t* drv)
{
    if (drv == NULL) return;
    drv->next = driver_list;
    driver_list = drv;
}

void driver_register_all(void)
{
    /* Platform-specific driver registration */
    printk(KERN_DEBUG "Driver: Registering all drivers\n");
}

int driver_probe_all(void)
{
    int count = 0;
    driver_t* drv = driver_list;
    
    while (drv != NULL) {
        if (drv->probe) {
            int ret = drv->probe();
            if (ret == 0) {
                printk(KERN_INFO "Driver: %s probed successfully\n", drv->name);
                count++;
            } else {
                printk(KERN_WARN "Driver: %s probe failed (%d)\n", drv->name, ret);
            }
        }
        drv = drv->next;
    }
    
    return count;
}

void driver_shutdown_all(void)
{
    driver_t* drv = driver_list;
    
    while (drv != NULL) {
        if (drv->shutdown) {
            drv->shutdown();
        }
        drv = drv->next;
    }
}
