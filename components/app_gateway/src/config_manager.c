/**
 * @file config_manager.c
 * @brief 应用层：动态配置管理器
 * @note V4.0 终极版：解析网关参数、多态传感器、反向控制权限以及边缘联动表达式规则
 */
#include "config_manager.h"
#include "bsp_fs.h"
#include "cJSON.h"
#include "register_map.h"
#include "esp_log.h"
#include "esp_mac.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "CFG_MGR";

static gateway_config_t g_gw_config = {0};
static char *g_json_cache = NULL;

// V4.0 边缘联动规则存储
static linkage_rule_t *g_linkage_rules = NULL;
static int g_linkage_count = 0;

const gateway_config_t* config_manager_get_gw_cfg(void) {
    return &g_gw_config;
}

const linkage_rule_t* config_manager_get_linkages(int *out_count) {
    if (out_count) *out_count = g_linkage_count;
    return g_linkage_rules;
}

char* config_manager_get_json(void) {
    if (g_json_cache) {
        return strdup(g_json_cache);
    }
    return bsp_fs_read_file_to_str("/vfs/sensors.json");
}

bool config_manager_save_json(const char *json_str) {
    if (!json_str) return false;
    
    // 保存至持久化文件系统
    if (!bsp_fs_write_str_to_file("/vfs/sensors.json", json_str)) {
        ESP_LOGE(TAG, "Failed to save sensors.json to VFS");
        return false;
    }
    
    if (g_json_cache) free(g_json_cache);
    g_json_cache = strdup(json_str);
    return true;
}

bool config_manager_load(sensor_device_t **out_sensors, int *out_count) {
    char *json_str = bsp_fs_read_file_to_str("/vfs/sensors.json");
    if (!json_str) {
        ESP_LOGE(TAG, "Failed to read sensors.json, using fallback.");
        return false;
    }

    if (g_json_cache) free(g_json_cache);
    g_json_cache = json_str; // 缓存进内存供 Web 和 MQTT 快速拉取

    cJSON *root = cJSON_Parse(json_str);
    if (!root) {
        ESP_LOGE(TAG, "JSON Parse Error: %s", cJSON_GetErrorPtr());
        return false;
    }

    // ==========================================
    // 1. 解析全局网关配置
    // ==========================================
    cJSON *gw_cfg_node = cJSON_GetObjectItem(root, "gateway_config");
    if (gw_cfg_node) {
        cJSON *dev_id_node = cJSON_GetObjectItem(gw_cfg_node, "device_id");
        if (dev_id_node && strlen(dev_id_node->valuestring) > 0 && strcmp(dev_id_node->valuestring, "AUTO") != 0) {
            strncpy(g_gw_config.device_id, dev_id_node->valuestring, sizeof(g_gw_config.device_id) - 1);
        } else {
            // 【量产兜底】：自动提取 MAC 地址作为设备唯一标识
            uint8_t mac[6];
            esp_read_mac(mac, ESP_MAC_WIFI_STA);
            snprintf(g_gw_config.device_id, sizeof(g_gw_config.device_id), "GW_%02X%02X%02X%02X%02X%02X", 
                     mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        }
        
        strncpy(g_gw_config.mqtt_broker, cJSON_GetObjectItem(gw_cfg_node, "mqtt_broker")->valuestring, sizeof(g_gw_config.mqtt_broker) - 1);
        
        cJSON *user_node = cJSON_GetObjectItem(gw_cfg_node, "mqtt_user");
        if (user_node) strncpy(g_gw_config.mqtt_user, user_node->valuestring, sizeof(g_gw_config.mqtt_user) - 1);
        
        cJSON *pass_node = cJSON_GetObjectItem(gw_cfg_node, "mqtt_pass");
        if (pass_node) strncpy(g_gw_config.mqtt_pass, pass_node->valuestring, sizeof(g_gw_config.mqtt_pass) - 1);
        
        g_gw_config.upload_interval_ms = cJSON_GetObjectItem(gw_cfg_node, "upload_interval_ms") ? cJSON_GetObjectItem(gw_cfg_node, "upload_interval_ms")->valueint : 10000;

        cJSON *subs = cJSON_GetObjectItem(gw_cfg_node, "sub_topics");
        g_gw_config.sub_topic_count = 0;
        if (subs && cJSON_IsArray(subs)) {
            int sub_cnt = cJSON_GetArraySize(subs);
            for (int i = 0; i < sub_cnt && i < 4; i++) {
                strncpy(g_gw_config.mqtt_sub_topics[i], cJSON_GetArrayItem(subs, i)->valuestring, 63);
                g_gw_config.sub_topic_count++;
            }
        }
        //  解析动态分配的串口字典
        cJSON *ports = cJSON_GetObjectItem(gw_cfg_node, "ports");
        g_gw_config.port_count = 0;
        if (ports && cJSON_IsArray(ports)) {
            int port_cnt = cJSON_GetArraySize(ports);
            for (int i = 0; i < port_cnt && i < 8; i++) {
                cJSON *p_node = cJSON_GetArrayItem(ports, i);
                serial_port_cfg_t *pcfg = &g_gw_config.ports[i];
                pcfg->port_id = cJSON_GetObjectItem(p_node, "port_id")->valueint;
                pcfg->hw_type = cJSON_GetObjectItem(p_node, "hw_type")->valueint;
                pcfg->role = cJSON_GetObjectItem(p_node, "role")->valueint;
                pcfg->baud_rate = cJSON_GetObjectItem(p_node, "baud_rate")->valueint;
                
                if (pcfg->hw_type == PORT_HW_NATIVE) {
                    pcfg->native_uart_num = cJSON_GetObjectItem(p_node, "native_uart_num")->valueint;
                    pcfg->tx_pin = cJSON_GetObjectItem(p_node, "tx_pin")->valueint;
                    pcfg->rx_pin = cJSON_GetObjectItem(p_node, "rx_pin")->valueint;
                    pcfg->rts_pin = cJSON_GetObjectItem(p_node, "rts_pin")->valueint;
                } else {
                    pcfg->i2c_addr = cJSON_GetObjectItem(p_node, "i2c_addr")->valueint;
                }
                
                if (pcfg->role == PORT_ROLE_SLAVE) {
                    pcfg->slave_id = cJSON_GetObjectItem(p_node, "slave_id") ? cJSON_GetObjectItem(p_node, "slave_id")->valueint : 1;
                }
                g_gw_config.port_count++;
            }
        }
    }

    // ==========================================
    // 2. 解析多态传感器与测点字典
    // ==========================================
    cJSON *sensors = cJSON_GetObjectItem(root, "sensors");
    if (sensors && cJSON_IsArray(sensors)) {
        *out_count = cJSON_GetArraySize(sensors);
        *out_sensors = malloc(sizeof(sensor_device_t) * (*out_count));
        
        for (int i = 0; i < *out_count; i++) {
            cJSON *sensor_node = cJSON_GetArrayItem(sensors, i);
            sensor_device_t *sensor = &(*out_sensors)[i];
            
            strncpy(sensor->name, cJSON_GetObjectItem(sensor_node, "name")->valuestring, sizeof(sensor->name) - 1);
            sensor->transport = cJSON_GetObjectItem(sensor_node, "transport")->valueint;
            sensor->protocol = cJSON_GetObjectItem(sensor_node, "protocol")->valueint;
            
            sensor->poll_interval_ms = cJSON_GetObjectItem(sensor_node, "poll_interval_ms") ? cJSON_GetObjectItem(sensor_node, "poll_interval_ms")->valueint : 1000;
            sensor->timeout_ms = cJSON_GetObjectItem(sensor_node, "timeout_ms") ? cJSON_GetObjectItem(sensor_node, "timeout_ms")->valueint : 500;
            sensor->slave_id = cJSON_GetObjectItem(sensor_node, "slave_id") ? cJSON_GetObjectItem(sensor_node, "slave_id")->valueint : 1;
            sensor->func_code = cJSON_GetObjectItem(sensor_node, "func_code") ? cJSON_GetObjectItem(sensor_node, "func_code")->valueint : 3;
            sensor->start_reg = cJSON_GetObjectItem(sensor_node, "start_reg") ? cJSON_GetObjectItem(sensor_node, "start_reg")->valueint : 0;
            sensor->reg_count = cJSON_GetObjectItem(sensor_node, "reg_count") ? cJSON_GetObjectItem(sensor_node, "reg_count")->valueint : 1;
            
            sensor->status_tag_id = cJSON_GetObjectItem(sensor_node, "status_tag_id") ? cJSON_GetObjectItem(sensor_node, "status_tag_id")->valueint : 0;

            // 注册设备的健康状态字测点 (不可北向写入，无持久化)
            if (sensor->status_tag_id > 0) {
                reg_map_add_tag(sensor->status_tag_id, sensor->name, TAG_TYPE_FLOAT32, true);
            }

            cJSON *rules = cJSON_GetObjectItem(sensor_node, "rules");
            if (rules && cJSON_IsArray(rules)) {
                sensor->rule_count = cJSON_GetArraySize(rules);
                sensor->rules = malloc(sizeof(modbus_mapping_rule_t) * sensor->rule_count);
                
                for (int j = 0; j < sensor->rule_count; j++) {
                    cJSON *rule = cJSON_GetArrayItem(rules, j);
                    modbus_mapping_rule_t *r = &sensor->rules[j];
                    
                    strncpy(r->name, cJSON_GetObjectItem(rule, "name")->valuestring, sizeof(r->name) - 1);
                    r->target_tag_id = cJSON_GetObjectItem(rule, "target_tag_id")->valueint;
                    r->offset = cJSON_GetObjectItem(rule, "offset")->valueint;
                    r->type = cJSON_GetObjectItem(rule, "type")->valueint;
                    r->scale = cJSON_GetObjectItem(rule, "scale") ? cJSON_GetObjectItem(rule, "scale")->valuedouble : 1.0;
                    
                    // V4.0 高级控制属性默认值
                    r->writable = false;
                    r->persist = false;
                    r->reverse_reg = 0xFFFF;
                    
                    cJSON *node_write = cJSON_GetObjectItem(rule, "writable");
                    if (node_write && cJSON_IsTrue(node_write)) r->writable = true;
                    
                    cJSON *node_persist = cJSON_GetObjectItem(rule, "persist");
                    if (node_persist && cJSON_IsTrue(node_persist)) r->persist = true;
                    
                    cJSON *node_rev = cJSON_GetObjectItem(rule, "reverse_reg");
                    if (node_rev) r->reverse_reg = node_rev->valueint;

                    // ⚡核心架构联动：将物理传感器的 slave_id 注入到虚拟测点的配置中
                    rtdb_tag_cfg_t tag_cfg = {
                        .type = TAG_TYPE_FLOAT32,
                        .writable = r->writable,
                        .persist = r->persist,
                        .reverse_reg = r->reverse_reg,
                        .slave_id = sensor->slave_id
                    };
                    strncpy(tag_cfg.name, r->name, sizeof(tag_cfg.name) - 1);
                    reg_map_add_tag_ext(r->target_tag_id, &tag_cfg);
                }
            }
        }
    }

    // ==========================================
    // 3. 解析边缘联动逻辑规则 (V4.0 字符串表达式版)
    // ==========================================
    cJSON *linkages = cJSON_GetObjectItem(root, "linkage_rules");
    if (linkages && cJSON_IsArray(linkages)) {
        g_linkage_count = cJSON_GetArraySize(linkages);
        if (g_linkage_count > 0) {
            g_linkage_rules = malloc(sizeof(linkage_rule_t) * g_linkage_count);
            for (int k = 0; k < g_linkage_count; k++) {
                cJSON *rule_node = cJSON_GetArrayItem(linkages, k);
                linkage_rule_t *rule = &g_linkage_rules[k];
                
                strncpy(rule->name, cJSON_GetObjectItem(rule_node, "name")->valuestring, sizeof(rule->name) - 1);
                rule->enable = cJSON_GetObjectItem(rule_node, "enable") ? cJSON_IsTrue(cJSON_GetObjectItem(rule_node, "enable")) : true;
                
                // 提取 condition 字符串表达式，供 utils_expr 引擎解析
                cJSON *cond_node = cJSON_GetObjectItem(rule_node, "condition");
                if (cond_node && cond_node->valuestring) {
                    strncpy(rule->condition, cond_node->valuestring, sizeof(rule->condition) - 1);
                } else {
                    rule->condition[0] = '\0';
                }
                
                rule->action_tag_id = cJSON_GetObjectItem(rule_node, "action_tag")->valueint;
                rule->action_value = cJSON_GetObjectItem(rule_node, "action_value")->valuedouble;
                rule->_last_state = false;
            }
        }
    }

    cJSON_Delete(root);
    ESP_LOGI(TAG, "Configuration Loaded. Sensors: %d, Linkage Rules: %d", *out_count, g_linkage_count);
    return true;
}