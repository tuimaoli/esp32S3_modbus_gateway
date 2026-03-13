/**
 * @file utils.c
 * @brief 中间件层：通用核心工具库实现
 */
#include "utils.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>

bool utils_validate_gateway_config(const char *json_str, char *err_buf, size_t err_len) {
    if (!json_str || !err_buf || err_len == 0) return false;

    cJSON *root = cJSON_Parse(json_str);
    if (!root) {
        snprintf(err_buf, err_len, "JSON Syntax Error: [%s]", cJSON_GetErrorPtr());
        return false;
    }

    // 1. 校验全局配置
    cJSON *gw_cfg = cJSON_GetObjectItem(root, "gateway_config");
    if (!gw_cfg) {
        snprintf(err_buf, err_len, "Missing [gateway_config] object");
        cJSON_Delete(root); return false;
    }

    // 2. 校验传感器数组
    cJSON *sensors = cJSON_GetObjectItem(root, "sensors");
    if (!sensors || !cJSON_IsArray(sensors)) {
        snprintf(err_buf, err_len, "Missing [sensors] array");
        cJSON_Delete(root); return false;
    }

    int sensor_count = cJSON_GetArraySize(sensors);
    for (int i = 0; i < sensor_count; i++) {
        cJSON *sensor = cJSON_GetArrayItem(sensors, i);
        if (!cJSON_GetObjectItem(sensor, "name") || !cJSON_GetObjectItem(sensor, "protocol")) {
            snprintf(err_buf, err_len, "Sensor[%d] missing 'name' or 'protocol'", i);
            cJSON_Delete(root); return false;
        }
        
        // 3. 校验 rules 映射关系
        cJSON *rules = cJSON_GetObjectItem(sensor, "rules");
        if (rules && cJSON_IsArray(rules)) {
            int rule_count = cJSON_GetArraySize(rules);
            for (int j = 0; j < rule_count; j++) {
                cJSON *rule = cJSON_GetArrayItem(rules, j);
                if (!cJSON_GetObjectItem(rule, "target_tag_id") || !cJSON_GetObjectItem(rule, "type")) {
                    snprintf(err_buf, err_len, "Sensor[%d] Rule[%d] missing 'target_tag_id' or 'type'", i, j);
                    cJSON_Delete(root); return false;
                }
            }
        }
    }

    cJSON_Delete(root);
    return true; 
}