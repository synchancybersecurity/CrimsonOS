#ifndef _CRIMSON_GPIO_H
#define _CRIMSON_GPIO_H

#include <crimson/types.h>

#define GPIO_IRQ_RISING     (1 << 0)
#define GPIO_IRQ_FALLING    (1 << 1)
#define GPIO_IRQ_HIGH       (1 << 2)
#define GPIO_IRQ_LOW        (1 << 3)

typedef void (*gpio_irq_handler_t)(uint32_t pin, void* data);

void gpio_init(void);
void gpio_set_function(uint32_t pin, uint32_t func);
void gpio_set_input(uint32_t pin);
void gpio_set_output(uint32_t pin);
void gpio_write(uint32_t pin, uint32_t value);
int gpio_read(uint32_t pin);
void gpio_set_pull(uint32_t pin, uint32_t pull);
void gpio_set_pull_up(uint32_t pin);
void gpio_set_pull_down(uint32_t pin);
void gpio_set_pull_off(uint32_t pin);
void gpio_toggle(uint32_t pin);
void gpio_enable_irq(uint32_t pin, uint32_t trigger, gpio_irq_handler_t handler, void* data);
void gpio_disable_irq(uint32_t pin);
void gpio_irq_handler(void);
void gpio_config_led(uint32_t pin);
void gpio_led_on(uint32_t pin);
void gpio_led_off(uint32_t pin);
void gpio_config_button(uint32_t pin);

#endif
