/**
 * @file modbus_slave.h
 * @brief 中间件层：Modbus RTU 从机引擎接口
 */
#pragma once
void modbus_slave_init(int uart_port, int slave_id);
void modbus_slave_loop(void);