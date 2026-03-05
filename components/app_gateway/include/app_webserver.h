/**
 * @file app_webserver.h
 * @brief 应用层：配置与监控 HTTP 服务器
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 启动局域网 WebServer
 * @note 依赖 LwIP/Wi-Fi 已成功初始化并获取 IP
 */
void app_webserver_start(void);

#ifdef __cplusplus
}
#endif