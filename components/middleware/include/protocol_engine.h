/**
 * @file protocol_engine.h
 * @brief 中间件层：泛化协议调度引擎 (替代原 modbus_master)
 * @note 支持 Modbus RTU/TCP 以及自定义非标协议(主从轮询 & 主动上报)
 */
#pragma once
#include <stdint.h>
#include "config_manager.h" // 依赖 V2.0 的 config_manager.h

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