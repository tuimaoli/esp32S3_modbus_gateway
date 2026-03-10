/**
 * @file config_manager.h
 * @brief 应用层：动态组态配置管理器 V2.0
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "modbus_template.h" 

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 网关全局配置 (V2.0)
 */
typedef struct {
    char device_id[32];           
    char mqtt_broker[64];         
    char mqtt_user[32];           
    char mqtt_pass[64];           
    char mqtt_sub_topics[4][64];  
    int  sub_topic_count;         
    uint32_t upload_interval_ms;  
} gateway_config_t;

/**
 * @brief 获取网关全局配置参数
 */
const gateway_config_t* config_manager_get_gw_cfg(void);

/**
 * @brief 从 LittleFS 加载 JSON 并转换为内存中的结构体数组
 */
bool config_manager_load(sensor_device_t **out_sensors, int *out_count);

/**
 * @brief 获取原始 JSON 字符串
 */
char* config_manager_get_json(void);

/**
 * @brief 接收下发的 JSON，校验格式后持久化写入 Flash
 */
bool config_manager_save_json(const char *json_str);

#ifdef __cplusplus
}
#endif