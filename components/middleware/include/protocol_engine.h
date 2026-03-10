/**
 * @file protocol_engine.h
 * @brief 中间件层：泛化协议调度引擎 
 * @note 仅依赖基础数据模板，解除与应用层的循环依赖
 */
#pragma once
#include <stdint.h>
#include "modbus_template.h" 

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化协议引擎 (分配硬件资源与底层队列)
 * @param uart_port RS485 绑定的 UART 端口号
 */
void protocol_engine_init(int uart_port);

/**
 * @brief 核心调度周期函数 (由 Gateway 任务循环调用)
 * @param sensors 动态传感器组态数组
 * @param count 传感器节点数量
 */
void protocol_engine_poll_cycle(const sensor_device_t *sensors, int count);

#ifdef __cplusplus
}
#endif