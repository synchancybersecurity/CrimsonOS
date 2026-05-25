/* Copyright (c) 2026 SynChanCyberSecurity LLC. All Rights Reserved. */
/*
 * Crimson OS - GPIO Driver
 * 
 * GPIO (General Purpose Input/Output) control for ARM64 platforms.
 * Controls LEDs, buttons, and general I/O pins.
 * 
 * Features:
 * - Pin mode configuration (input/output/alt function)
 * - Pull-up/pull-down control
 * - Interrupt on edge/level detection
 * - Batch operations for efficiency
 */

#include <crimson/types.h>
#include <crimson/gpio.h>
#include <crimson/interrupt.h>
#include <crimson/printk.h>

/* BCM2711 (RPi4) GPIO Base */
#ifdef BOARD_RPI4
  #define GPIO_BASE       0xFE200000
  #define GPIO_IRQ        49    /* GPIO IRQ number */
#elif defined(BOARD_RPI3)
  #define GPIO_BASE       0x3F200000
  #define GPIO_IRQ        49
#else
  #define GPIO_BASE       0x09000000
  #define GPIO_IRQ        0
#endif

/* GPIO Register Offsets */
#define GPFSEL0         0x00    /* Function Select 0 (pins 0-9) */
#define GPFSEL1         0x04    /* Function Select 1 (pins 10-19) */
#define GPFSEL2         0x08    /* Function Select 2 (pins 20-29) */
#define GPFSEL3         0x0C    /* Function Select 3 (pins 30-39) */
#define GPFSEL4         0x10    /* Function Select 4 (pins 40-49) */
#define GPFSEL5         0x14    /* Function Select 5 (pins 50-53) */

#define GPSET0          0x1C    /* Pin Output Set 0 */
#define GPSET1          0x20    /* Pin Output Set 1 */
#define GPCLR0          0x28    /* Pin Output Clear 0 */
#define GPCLR1          0x2C    /* Pin Output Clear 1 */
#define GPLEV0          0x34    /* Pin Level 0 */
#define GPLEV1          0x38    /* Pin Level 1 */
#define GPEDS0          0x40    /* Pin Event Detect Status 0 */
#define GPEDS1          0x44    /* Pin Event Detect Status 1 */
#define GPREN0          0x4C    /* Pin Rising Edge Detect Enable 0 */
#define GPREN1          0x50    /* Pin Rising Edge Detect Enable 1 */
#define GPFEN0          0x58    /* Pin Falling Edge Detect Enable 0 */
#define GPFEN1          0x5C    /* Pin Falling Edge Detect Enable 1 */
#define GPHEN0          0x64    /* Pin High Detect Enable 0 */
#define GPHEN1          0x68    /* Pin High Detect Enable 1 */
#define GPLEN0          0x70    /* Pin Low Detect Enable 0 */
#define GPLEN1          0x74    /* Pin Low Detect Enable 1 */
#define GPAREN0         0x7C    /* Pin Async Rising Edge Detect 0 */
#define GPAFEN0         0x88    /* Pin Async Falling Edge Detect 0 */
#define GPPUD           0x94    /* Pin Pull-up/down Enable */
#define GPPUDCLK0       0x98    /* Pin Pull-up/down Enable Clock 0 */
#define GPPUDCLK1       0x9C    /* Pin Pull-up/down Enable Clock 1 */

/* BCM2711 has different pull-up/down registers */
#define GPIO_PUP_PDN_CNTRL_REG0  0xE4
#define GPIO_PUP_PDN_CNTRL_REG1  0xE8

/* Function select values (3 bits per pin) */
#define FSEL_INPUT      0x0
#define FSEL_OUTPUT     0x1
#define FSEL_ALT0       0x4
#define FSEL_ALT1       0x5
#define FSEL_ALT2       0x6
#define FSEL_ALT3       0x7
#define FSEL_ALT4       0x3
#define FSEL_ALT5       0x2

/* Pull-up/down values for BCM2711 */
#define PULL_NONE       0x0
#define PULL_UP         0x1
#define PULL_DOWN       0x2

/* Number of GPIO pins */
#define NUM_GPIO_PINS   54

static volatile uint32_t* gpio_base = NULL;
static gpio_irq_handler_t gpio_handlers[NUM_GPIO_PINS];
static void* gpio_handler_data[NUM_GPIO_PINS];

#define GPIO_REG(off)   (*(gpio_base + ((off) / 4)))

/*
 * gpio_init - Initialize GPIO subsystem
 */
void gpio_init(void)
{
    gpio_base = (volatile uint32_t*)GPIO_BASE;
    
    /* Clear all handlers */
    for (int i = 0; i < NUM_GPIO_PINS; i++) {
        gpio_handlers[i] = NULL;
        gpio_handler_data[i] = NULL;
    }
    
    /* Clear all event detect status */
    GPIO_REG(GPEDS0) = 0xFFFFFFFF;
    GPIO_REG(GPEDS1) = 0xFFFFFFFF;
    
    printk(KERN_DEBUG "GPIO: Initialized, %d pins available\n", NUM_GPIO_PINS);
}

/*
 * gpio_set_function - Set pin function
 * @pin: GPIO pin number (0-53)
 * @func: Function (FSEL_xxx)
 */
void gpio_set_function(uint32_t pin, uint32_t func)
{
    if (pin >= NUM_GPIO_PINS) return;
    if (func > 0x7) return;
    
    uint32_t reg = GPFSEL0 + (pin / 10) * 4;
    uint32_t shift = (pin % 10) * 3;
    
    uint32_t val = GPIO_REG(reg);
    val &= ~(0x7 << shift);           /* Clear current function */
    val |= (func & 0x7) << shift;     /* Set new function */
    GPIO_REG(reg) = val;
}

/*
 * gpio_set_input - Configure pin as input
 */
void gpio_set_input(uint32_t pin)
{
    gpio_set_function(pin, FSEL_INPUT);
}

/*
 * gpio_set_output - Configure pin as output
 */
void gpio_set_output(uint32_t pin)
{
    gpio_set_function(pin, FSEL_OUTPUT);
}

/*
 * gpio_write - Set pin output level
 * @pin: GPIO pin
 * @value: 0 (low) or 1 (high)
 */
void gpio_write(uint32_t pin, uint32_t value)
{
    if (pin >= NUM_GPIO_PINS) return;
    
    if (value) {
        if (pin < 32) {
            GPIO_REG(GPSET0) = (1 << pin);
        } else {
            GPIO_REG(GPSET1) = (1 << (pin - 32));
        }
    } else {
        if (pin < 32) {
            GPIO_REG(GPCLR0) = (1 << pin);
        } else {
            GPIO_REG(GPCLR1) = (1 << (pin - 32));
        }
    }
}

/*
 * gpio_read - Read pin input level
 * Returns: 0 (low) or 1 (high)
 */
int gpio_read(uint32_t pin)
{
    if (pin >= NUM_GPIO_PINS) return 0;
    
    if (pin < 32) {
        return (GPIO_REG(GPLEV0) >> pin) & 1;
    } else {
        return (GPIO_REG(GPLEV1) >> (pin - 32)) & 1;
    }
}

/*
 * gpio_set_pull - Set pull-up/down resistor
 * @pin: GPIO pin
 * @pull: PULL_NONE, PULL_UP, or PULL_DOWN
 */
void gpio_set_pull(uint32_t pin, uint32_t pull)
{
    if (pin >= NUM_GPIO_PINS) return;
    if (pull > 2) return;
    
#ifdef BOARD_RPI4
    /* BCM2711 uses separate pull-up/down registers */
    uint32_t reg = GPIO_PUP_PDN_CNTRL_REG0 + (pin / 16) * 4;
    uint32_t shift = (pin % 16) * 2;
    
    uint32_t val = GPIO_REG(reg);
    val &= ~(0x3 << shift);
    val |= (pull & 0x3) << shift;
    GPIO_REG(reg) = val;
#else
    /* Legacy method for older BCM chips */
    GPIO_REG(GPPUD) = pull;
    for (volatile int i = 0; i < 150; i++) __asm__ volatile("nop");
    
    if (pin < 32) {
        GPIO_REG(GPPUDCLK0) = (1 << pin);
    } else {
        GPIO_REG(GPPUDCLK1) = (1 << (pin - 32));
    }
    for (volatile int i = 0; i < 150; i++) __asm__ volatile("nop");
    
    GPIO_REG(GPPUD) = 0;
    GPIO_REG(GPPUDCLK0) = 0;
    GPIO_REG(GPPUDCLK1) = 0;
#endif
}

/*
 * gpio_set_pull_up - Enable pull-up resistor
 */
void gpio_set_pull_up(uint32_t pin)
{
    gpio_set_pull(pin, PULL_UP);
}

/*
 * gpio_set_pull_down - Enable pull-down resistor
 */
void gpio_set_pull_down(uint32_t pin)
{
    gpio_set_pull(pin, PULL_DOWN);
}

/*
 * gpio_set_pull_off - Disable pull resistor
 */
void gpio_set_pull_off(uint32_t pin)
{
    gpio_set_pull(pin, PULL_NONE);
}

/*
 * gpio_toggle - Toggle pin output
 */
void gpio_toggle(uint32_t pin)
{
    gpio_write(pin, !gpio_read(pin));
}

/* ─── Interrupt Support ─── */

/*
 * gpio_enable_irq - Enable interrupt on pin
 * @pin: GPIO pin
 * @trigger: Trigger type (GPIO_IRQ_RISING, GPIO_IRQ_FALLING, etc.)
 * @handler: Callback function
 * @data: User data passed to handler
 */
void gpio_enable_irq(uint32_t pin, uint32_t trigger, gpio_irq_handler_t handler, void* data)
{
    if (pin >= NUM_GPIO_PINS) return;
    
    gpio_handlers[pin] = handler;
    gpio_handler_data[pin] = data;
    
    /* Configure edge/level detection */
    uint32_t mask;
    if (pin < 32) {
        mask = (1 << pin);
        
        if (trigger & GPIO_IRQ_RISING) {
            GPIO_REG(GPREN0) |= mask;
        }
        if (trigger & GPIO_IRQ_FALLING) {
            GPIO_REG(GPFEN0) |= mask;
        }
        if (trigger & GPIO_IRQ_HIGH) {
            GPIO_REG(GPHEN0) |= mask;
        }
        if (trigger & GPIO_IRQ_LOW) {
            GPIO_REG(GPLEN0) |= mask;
        }
    } else {
        mask = (1 << (pin - 32));
        /* GPREN1, GPFEN1, etc. - similar pattern */
    }
    
    /* Enable GPIO interrupt in GIC */
    interrupt_enable(GPIO_IRQ);
}

/*
 * gpio_disable_irq - Disable interrupts on pin
 */
void gpio_disable_irq(uint32_t pin)
{
    if (pin >= NUM_GPIO_PINS) return;
    
    uint32_t mask;
    if (pin < 32) {
        mask = (1 << pin);
        GPIO_REG(GPREN0) &= ~mask;
        GPIO_REG(GPFEN0) &= ~mask;
        GPIO_REG(GPHEN0) &= ~mask;
        GPIO_REG(GPLEN0) &= ~mask;
    }
    
    gpio_handlers[pin] = NULL;
    gpio_handler_data[pin] = NULL;
}

/*
 * gpio_irq_handler - GPIO interrupt handler (called by GIC)
 */
void gpio_irq_handler(void)
{
    /* Check which pins triggered */
    uint32_t status0 = GPIO_REG(GPEDS0);
    uint32_t status1 = GPIO_REG(GPEDS1);
    
    /* Handle pins 0-31 */
    for (int pin = 0; pin < 32; pin++) {
        if (status0 & (1 << pin)) {
            /* Clear event */
            GPIO_REG(GPEDS0) = (1 << pin);
            
            /* Call handler */
            if (gpio_handlers[pin]) {
                gpio_handlers[pin](pin, gpio_handler_data[pin]);
            }
        }
    }
    
    /* Handle pins 32-53 */
    for (int pin = 32; pin < NUM_GPIO_PINS; pin++) {
        if (status1 & (1 << (pin - 32))) {
            GPIO_REG(GPEDS1) = (1 << (pin - 32));
            
            if (gpio_handlers[pin]) {
                gpio_handlers[pin](pin, gpio_handler_data[pin]);
            }
        }
    }
}

/* ─── Platform Configuration ─── */

/*
 * gpio_config_led - Configure a pin as activity LED
 */
void gpio_config_led(uint32_t pin)
{
    gpio_set_output(pin);
    gpio_set_pull_off(pin);
    gpio_write(pin, 0);
}

/*
 * gpio_led_on - Turn LED on
 */
void gpio_led_on(uint32_t pin)
{
    gpio_write(pin, 1);
}

/*
 * gpio_led_off - Turn LED off
 */
void gpio_led_off(uint32_t pin)
{
    gpio_write(pin, 0);
}

/*
 * gpio_config_button - Configure pin as input button
 */
void gpio_config_button(uint32_t pin)
{
    gpio_set_input(pin);
    gpio_set_pull_up(pin);
}
