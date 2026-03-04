#pragma once

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

/**
 * @brief I2C 初始化配置
 */
typedef struct {
    int sda_io;
    int scl_io;
    uint32_t clk_speed;
    int port_num;
} bsp_i2c_config_t;

/**
 * @brief 初始化硬件 I2C
 */
esp_err_t bsp_i2c_init(const bsp_i2c_config_t *conf);

/**
 * @brief 通用 I2C 写数据
 */
esp_err_t bsp_i2c_write(int port, uint8_t addr, const uint8_t *data, size_t len);

/**
 * @brief 通用 I2C 读数据
 */
esp_err_t bsp_i2c_read(int port, uint8_t addr, uint8_t *buffer, size_t len);