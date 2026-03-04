/**
 * @file bsp_gpio.c
 * @brief BSP层：通用 GPIO 操作实现
 */
#include "bsp_gpio.h"
#include "driver/gpio.h"

void bsp_gpio_set_level(int gpio_num, bool level) { 
    gpio_set_level((gpio_num_t)gpio_num, level ? 1 : 0);
}

bool bsp_gpio_get_level(int gpio_num) { 
    return gpio_get_level((gpio_num_t)gpio_num) == 1; 
}