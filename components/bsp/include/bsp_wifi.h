/**
 * @file bsp_wifi.h
 * @brief BSP层：ESP32-S3 Wi-Fi 硬件抽象接口
 * @note 封装了 LwIP 网络接口初始化与 STA 模式连接逻辑，对上层屏蔽事件循环细节
 */
#pragma once

#include <stdbool.h>
#include "esp_err.h"

/**
 * @brief Wi-Fi 站点 (STA) 配置描述符
 */
typedef struct {
    char ssid[32];      ///< Wi-Fi 热点名称
    char password[64];  ///< Wi-Fi 密码
} bsp_wifi_config_t;

/**
 * @brief 初始化 Wi-Fi 并启动非阻塞连接
 * @param config 包含 SSID 和密码的配置结构体指针
 * @return ESP_OK: 启动成功; 其他: 硬件初始化失败
 */
esp_err_t bsp_wifi_init(const bsp_wifi_config_t *config);

/**
 * @brief 查询当前 Wi-Fi 是否已成功获取 IP 地址
 * @return true: 已连接且有 IP; false: 未连接或正在分配
 */
bool bsp_wifi_is_connected(void);