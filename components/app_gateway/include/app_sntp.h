/**
 * @file app_sntp.h
 * @brief 网络时间同步引擎 (SNTP)
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 启动后台网络校时服务
 * @note 必须在网络（WiFi或以太网）获取到 IP 后调用
 */
void app_sntp_init(void);

/**
 * @brief 获取当前的标准 ISO8601 格式时间字符串
 * @param out_str 接收字符串的缓存 (至少32字节)
 */
void app_sntp_get_iso8601(char *out_str);

#ifdef __cplusplus
}
#endif