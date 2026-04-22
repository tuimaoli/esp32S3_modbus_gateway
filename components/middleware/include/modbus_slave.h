/**
 * @file modbus_slave.h
 * @brief 中间件层：Modbus RTU 从机引擎接口 (OOP 多态架构版)
 * @note 接收面向对象的 bsp_serial_port_t 句柄，彻底屏蔽底层硬件差异
 */
#pragma once
#include "bsp_serial_port.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 接收面向对象端口实例，自动在后台拉起从机响应多线程
 * @param port  OOP 串口实例句柄 (原生 UART 或 I2C 扩展 UART)
 * @param slave_id 本网关对外响应的 Modbus 从机站号
 */
void modbus_slave_start_worker(bsp_serial_port_t *port, int slave_id);

#ifdef __cplusplus
}
#endif