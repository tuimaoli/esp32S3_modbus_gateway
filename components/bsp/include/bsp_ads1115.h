/**
 * @file bsp_ads1115.h
 * @brief BSP层：ADS1115 16位高精度 ADC 驱动接口
 * @note 纯面向对象抽象，基于现有的通用 bsp_i2c 接口封装
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ADS1115_I2C_ADDR_GND  0x48  ///< ADDR 引脚接 GND 时的 I2C 地址
#define ADS1115_I2C_ADDR_VDD  0x49  ///< ADDR 引脚接 VDD 时的 I2C 地址

/**
 * @brief ADS1115 内部可编程增益放大器 (PGA) 量程
 */
typedef enum {
    ADS1115_PGA_6_144V = 0,  ///< +/- 6.144V
    ADS1115_PGA_4_096V,      ///< +/- 4.096V
    ADS1115_PGA_2_048V,      ///< +/- 2.048V (默认)
    ADS1115_PGA_1_024V,      ///< +/- 1.024V
    ADS1115_PGA_0_512V,      ///< +/- 0.512V
    ADS1115_PGA_0_256V       ///< +/- 0.256V
} ads1115_pga_e;

/**
 * @brief 读取 ADS1115 单端模拟通道的电压值
 * @param i2c_port 绑定的 I2C 硬件端口号 (通常为 0)
 * @param i2c_addr ADS1115 的 I2C 物理地址 (如 0x48)
 * @param channel 要读取的模拟通道 (0 ~ 3 代表 AIN0 ~ AIN3)
 * @param pga 增益量程配置
 * @param out_voltage 用于接收解码后真实电压值的指针 (单位：伏特 V)
 * @return true: 读取成功; false: 总线通信失败
 */
bool bsp_ads1115_read_single_ended(int i2c_port, uint8_t i2c_addr, uint8_t channel, ads1115_pga_e pga, float *out_voltage);

#ifdef __cplusplus
}
#endif