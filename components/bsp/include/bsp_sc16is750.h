/**
 * @file bsp_sc16is750.h
 * @brief BSP层：SC16IS750 (I2C/SPI to UART) 拓展芯片驱动
 * @note 支持内部 Auto-RS485 流控，对接 bsp_serial_port_t 抽象接口
 */
#pragma once

#include "bsp_serial_port.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 实例化一个 SC16IS750 拓展串口对象
 * * @param logical_port_id JSON 组态分配的逻辑端口号
 * @param i2c_port 绑定的 I2C 硬件端口号 (通常为 0)
 * @param i2c_addr SC16IS750 的 I2C 物理地址 (由 A0, A1 引脚决定，如 0x4D)
 * @param baud_rate 目标波特率 (如 9600)
 * @param xtal_freq_hz 拓展芯片外挂晶振频率 (如 14.7456MHz = 14745600)
 * @return bsp_serial_port_t* 成功返回 OOP 串口句柄，失败返回 NULL
 */
bsp_serial_port_t* bsp_sc16is750_create(int logical_port_id, int i2c_port, uint8_t i2c_addr, int baud_rate, uint32_t xtal_freq_hz);

#ifdef __cplusplus
}
#endif