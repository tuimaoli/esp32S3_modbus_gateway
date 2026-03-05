/**
 * @file config_manager.h
 * @brief 应用层：动态组态配置管理器
 * @note 负责从 JSON 中反序列化传感器轮询规则，并自动注册到 RTDB
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "modbus_master.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 加载并解析配置文件，生成动态轮询列表
 * @param[out] out_sensors 用于接收动态分配的传感器数组指针
 * @param[out] out_count 用于接收传感器总数
 * @return true 成功; false 解析失败或文件不存在
 */
bool config_manager_load(sensor_device_t **out_sensors, int *out_count);

/**
 * @brief 获取当前的 JSON 配置原始字符串
 */
char* config_manager_get_json(void);

/**
 * @brief 保存新的 JSON 配置文件
 */
bool config_manager_save_json(const char *json_str);

#ifdef __cplusplus
}
#endif