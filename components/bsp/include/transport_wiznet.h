/**
 * @file transport_wiznet.h
 * @brief BSP层：WIZnet 硬件 TCP 协议栈的 ESP-Transport 适配器
 * @note 允许 ESP-IDF 的原生 MQTT、HTTP 客户端直接运行在 W5100S 硬件栈上
 */
#pragma once
#include "esp_transport.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 创建并初始化 WIZnet 传输层句柄
 * @return esp_transport_handle_t 传输层实例
 */
esp_transport_handle_t esp_transport_wiznet_init(void);

#ifdef __cplusplus
}
#endif