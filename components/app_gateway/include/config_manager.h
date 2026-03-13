/**
 * @file config_manager.h
 * @brief 应用层：动态配置管理器头文件
 */
#pragma once

#include "protocol_engine.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * V4.0 新增：边缘联动规则 (Edge Linkage Rule) 数据模型
 * ============================================================ */
typedef struct {
    char name[32];
    bool enable;
    
    // 条件部分 (Condition) - 直接容纳表达式字符串
    char condition[128]; 
    
    // 动作部分 (Action)
    uint16_t action_tag_id;
    float action_value;
    
    // 运行期状态 (防止重复触发的边沿检测)
    bool _last_state; 
} linkage_rule_t;

/* ============================================================
 * V4.1 重构：本地物理/系统测点的高级组态模型 (彻底消灭硬编码)
 * ============================================================ */
typedef struct {
    uint16_t tag_id;      // 目标本地测点 (如 501 继电器, 510 ADC)
    bool     persist;     // 是否开启掉电记忆
    float    scale;       // 线性缩放系数 (供 ADC 转换为实际物理量，默认 1.0)
    float    offset;      // 线性偏移量 (校准调零用，默认 0.0)
} local_tag_cfg_t;

/**
 * @brief 全局网关基础配置
 */
typedef struct {
    char device_id[32];
    char mqtt_broker[64];
    char mqtt_user[32];
    char mqtt_pass[32];
    
    char mqtt_sub_topics[4][64]; // 最多支持4个动态订阅主题
    int  sub_topic_count;
    int32_t upload_interval_ms;  // 改为有符号 32 位整型，<=0 代表关闭主动上报

    // V4.1 彻底解耦：支持任意数量本地 IO 的动态配置与标定
    local_tag_cfg_t local_tags[16]; 
    int local_tag_count;
} gateway_config_t;

/* ============================================================
 * 对外接口
 * ============================================================ */

/**
 * @brief 获取网关全局配置参数
 */
const gateway_config_t* config_manager_get_gw_cfg(void);

/**
 * @brief 解析 JSON 文件并动态生成传感器画像数组
 */
bool config_manager_load(sensor_device_t **out_sensors, int *out_count);

/**
 * @brief 获取当前加载的联动规则列表
 */
const linkage_rule_t* config_manager_get_linkages(int *out_count);

/**
 * @brief 获取当前 JSON 配置字符串 (供 Web 前端读取)
 */
char* config_manager_get_json(void);

/**
 * @brief 将 JSON 字符串持久化保存并更新内存缓存
 */
bool config_manager_save_json(const char *json_str);

#ifdef __cplusplus
}
#endif