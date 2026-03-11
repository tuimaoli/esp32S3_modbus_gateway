/**
 * @file config_manager.h
 * @brief 应用层：动态组态配置管理器 V2.0
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>
// 架构修正：正常包含下层引擎的头文件以获取 sensor_device_t 定义
#include "protocol_engine.h" 

#ifdef __cplusplus
extern "C" {
#endif

// ==========================================
// V2.0 新增：网关全局配置 (App 层专属配置，不影响底层，保留在此)
// ==========================================
typedef struct {
    char device_id[32];
    char mqtt_broker[64];
    char mqtt_user[32];
    char mqtt_pass[64];
    char mqtt_sub_topics[4][64]; // 最多支持4个动态订阅主题
    int  sub_topic_count;
    uint32_t upload_interval_ms;
} gateway_config_t;

/**
 * @brief 获取网关全局配置参数
 */
const gateway_config_t* config_manager_get_gw_cfg(void);

/**
 * @brief 解析 JSON 文件并动态生成传感器画像数组
 */
bool config_manager_load(sensor_device_t **out_sensors, int *out_count);

/**
 * @brief 获取当前 JSON 配置字符串 (供 Web 前端读取)
 */
char* config_manager_get_json(void);

/**
 * @brief 校验并保存新 JSON 配置到文件系统
 */
bool config_manager_save_json(const char *json_str);

#ifdef __cplusplus
}
#endif