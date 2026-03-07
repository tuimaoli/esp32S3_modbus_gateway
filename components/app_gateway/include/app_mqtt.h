/**
 * @file app_mqtt.h
 * @brief 应用层：MQTT 云端通讯引擎 (带 FreeRTOS 异步队列解耦)
 */
#pragma once
#include "config_manager.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 单点数据上报消息结构体 (压入队列使用)
 */
typedef struct {
    char sensor_name[32];
    char metric_name[32];
    float value;
} mqtt_msg_t;

/**
 * @brief 启动 MQTT 客户端守护进程与 TX 队列引擎
 * @param sensors 指向网关当前管理的动态传感器数组
 * @param sensor_count 传感器数量
 */
void app_mqtt_start(const sensor_device_t *sensors, int sensor_count);

/**
 * @brief 将采集到的最新数据压入 MQTT 发送队列
 * @note 此函数极快，非阻塞，供底层轮询引擎在解析完毕后立即调用
 */
void app_mqtt_enqueue_data(const char *sensor_name, const char *metric_name, float value);

#ifdef __cplusplus
}
#endif