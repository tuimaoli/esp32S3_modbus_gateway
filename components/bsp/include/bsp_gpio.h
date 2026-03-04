/**
 * @file bsp_gpio.h
 * @brief BSP层：通用 GPIO 操作接口
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>

void bsp_gpio_set_level(int gpio_num, bool level);
bool bsp_gpio_get_level(int gpio_num);