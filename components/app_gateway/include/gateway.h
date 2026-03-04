/**
 * @file gateway.h
 * @brief 应用层：工业网关核心引擎总控接口
 * @note 负责组装底层驱动、中间件与业务逻辑任务
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化网关的所有基础设施
 * @note 包括：初始化硬件接口 (UART/I2C/SPI)、分配 RTDB 内存、构建内部数据字典映射
 */
void gateway_init(void);

/**
 * @brief 启动网关的各项常驻守护任务
 * @note 包括：IO扫描任务、Modbus主机轮询任务、Modbus从机响应任务、硬件 TCP 服务端任务等
 */
void gateway_start(void);

#ifdef __cplusplus
}
#endif