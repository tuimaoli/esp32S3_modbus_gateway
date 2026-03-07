/**
 * @file config_manager.c
 * @brief 应用层：配置管理器实现 V2.0 (支持非标协议解析)
 */
#include "config_manager.h"
#include "bsp_fs.h"
#include "register_map.h"
#include "modbus_template.h"
#include "cJSON.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

static const char *TAG = "CONFIG_MGR";
#define CONFIG_FILE_PATH "/vfs/sensors.json"

static gateway_config_t g_gw_config = {0};
static sensor_profile_t *g_dynamic_profiles = NULL;
static int g_profile_count = 0;

/* ============================================================
 * 出厂默认 V2.0 JSON 模板 (包含网关配置、Modbus 与非标案例)
 * ============================================================ */
static const char* DEFAULT_JSON_CONFIG = 
"{\n"
"  \"version\": \"2.0\",\n"
"  \"gateway_config\": {\n"
"    \"device_id\": \"GW_ESP32S3_001\",\n"
"    \"mqtt_broker\": \"mqtt://192.168.1.50:1883\",\n"
"    \"mqtt_user\": \"admin\",\n"
"    \"mqtt_pass\": \"public\",\n"
"    \"mqtt_sub_topics\": [\"/gw/cmd/GW_ESP32S3_001\", \"/broadcast/sync\"],\n"
"    \"upload_interval_ms\": 5000\n"
"  },\n"
"  \"sensors\": [\n"
"    {\n"
"      \"name\": \"Room1_TH_Modbus\",\n"
"      \"transport\": 0,\n"
"      \"protocol\": \"MODBUS_RTU\",\n"
"      \"poll_interval_ms\": 1000,\n"
"      \"timeout_ms\": 200,\n"
"      \"slave_id\": 1,\n"
"      \"func_code\": 3,\n"
"      \"start_reg\": 0,\n"
"      \"reg_count\": 2,\n"
"      \"status_tag_id\": 199,\n"
"      \"rules\": [\n"
"        { \"name\": \"Room1_Temp\", \"tag_id\": 100, \"offset\": 0, \"type\": 1, \"scale\": 0.1 }\n"
"      ]\n"
"    },\n"
"    {\n"
"      \"name\": \"Custom_Weight_Scale\",\n"
"      \"transport\": 0,\n"
"      \"protocol\": \"CUSTOM_POLL\",\n"
"      \"poll_interval_ms\": 500,\n"
"      \"timeout_ms\": 100,\n"
"      \"status_tag_id\": 200,\n"
"      \"custom_cfg\": {\n"
"        \"tx_hex\": \"52 45 41 44\",\n"
"        \"frame_mode\": \"HEAD_TAIL\",\n"
"        \"header\": \"AA 55\",\n"
"        \"footer\": \"0D 0A\"\n"
"      },\n"
"      \"rules\": [\n"
"        { \"name\": \"Weight_KG\", \"tag_id\": 201, \"offset\": 2, \"type\": 2, \"scale\": 0.01 }\n"
"      ]\n"
"    }\n"
"  ]\n"
"}";

/**
 * @brief 工具函数：将 "AA 55 0D" 等空格分隔的 Hex 字符串转为 byte 数组
 */
static int hex_str_to_bytes(const char *hex_str, uint8_t *out_buf, int max_len) {
    if (!hex_str) return 0;
    int len = 0;
    const char *p = hex_str;
    while (*p && len < max_len) {
        if (isspace((unsigned char)*p)) { p++; continue; }
        if (isxdigit((unsigned char)p[0]) && isxdigit((unsigned char)p[1])) {
            char byte_str[3] = {p[0], p[1], '\0'};
            out_buf[len++] = (uint8_t)strtol(byte_str, NULL, 16);
            p += 2;
        } else {
            p++;
        }
    }
    return len;
}

static void generic_parser_wrapper(const uint8_t *rx_buf, uint16_t len, uint16_t profile_index) {
    if (len < 5 || profile_index >= g_profile_count) return;
    uint8_t byte_count = rx_buf[2];
    if (len < (3 + byte_count)) return;
    modbus_universal_parser(&rx_buf[3], byte_count, &g_dynamic_profiles[profile_index]);
}

const gateway_config_t* config_manager_get_gw_cfg(void) {
    return &g_gw_config;
}

bool config_manager_load(sensor_device_t **out_sensors, int *out_count) {
    char *json_str = bsp_fs_read_file_to_str(CONFIG_FILE_PATH);
    if (!json_str) {
        ESP_LOGW(TAG, "Config file not found. Generating V2.0 factory default...");
        bsp_fs_write_str_to_file(CONFIG_FILE_PATH, DEFAULT_JSON_CONFIG);
        json_str = strdup(DEFAULT_JSON_CONFIG); 
    }

    cJSON *root = cJSON_Parse(json_str);
    free(json_str);
    if (!root) return false;

    // 1. 解析网关全局配置
    cJSON *gw_node = cJSON_GetObjectItem(root, "gateway_config");
    if (gw_node) {
        strncpy(g_gw_config.device_id, cJSON_GetObjectItem(gw_node, "device_id")->valuestring, 31);
        strncpy(g_gw_config.mqtt_broker, cJSON_GetObjectItem(gw_node, "mqtt_broker")->valuestring, 63);
        
        cJSON *user_node = cJSON_GetObjectItem(gw_node, "mqtt_user");
        if (user_node) strncpy(g_gw_config.mqtt_user, user_node->valuestring, 31);
        
        cJSON *pass_node = cJSON_GetObjectItem(gw_node, "mqtt_pass");
        if (pass_node) strncpy(g_gw_config.mqtt_pass, pass_node->valuestring, 63);
        
        cJSON *sub_arr = cJSON_GetObjectItem(gw_node, "mqtt_sub_topics");
        if (cJSON_IsArray(sub_arr)) {
            g_gw_config.sub_topic_count = cJSON_GetArraySize(sub_arr);
            if (g_gw_config.sub_topic_count > 4) g_gw_config.sub_topic_count = 4; // 限制最大订阅数防越界
            for (int i = 0; i < g_gw_config.sub_topic_count; i++) {
                strncpy(g_gw_config.mqtt_sub_topics[i], cJSON_GetArrayItem(sub_arr, i)->valuestring, 63);
            }
        }
        
        g_gw_config.upload_interval_ms = cJSON_GetObjectItem(gw_node, "upload_interval_ms")->valueint;
    }

    // 2. 解析多态传感器节点
    cJSON *sensors_arr = cJSON_GetObjectItem(root, "sensors");
    if (!cJSON_IsArray(sensors_arr)) { cJSON_Delete(root); return false; }

    int sensor_cnt = cJSON_GetArraySize(sensors_arr);
    if (sensor_cnt == 0) { cJSON_Delete(root); *out_sensors = NULL; *out_count = 0; return true; }

    sensor_device_t *dev_array = calloc(sensor_cnt, sizeof(sensor_device_t));
    g_dynamic_profiles = calloc(sensor_cnt, sizeof(sensor_profile_t));
    g_profile_count = sensor_cnt;

    for (int i = 0; i < sensor_cnt; i++) {
        cJSON *item = cJSON_GetArrayItem(sensors_arr, i);
        sensor_device_t *dev = &dev_array[i];
        sensor_profile_t *prof = &g_dynamic_profiles[i];

        // 基础参数
        strncpy(dev->name, cJSON_GetObjectItem(item, "name")->valuestring, 31);
        dev->transport = cJSON_GetObjectItem(item, "transport")->valueint;
        
        // 解析协议与高级调度参数 (带默认值回退)
        cJSON *proto_node = cJSON_GetObjectItem(item, "protocol");
        if (proto_node && strcmp(proto_node->valuestring, "CUSTOM_POLL") == 0) dev->protocol = PROTO_CUSTOM_POLL;
        else if (proto_node && strcmp(proto_node->valuestring, "CUSTOM_REPORT") == 0) dev->protocol = PROTO_CUSTOM_REPORT;
        else dev->protocol = PROTO_MODBUS_RTU;

        dev->poll_interval_ms = cJSON_GetObjectItem(item, "poll_interval_ms") ? cJSON_GetObjectItem(item, "poll_interval_ms")->valueint : 1000;
        dev->timeout_ms = cJSON_GetObjectItem(item, "timeout_ms") ? cJSON_GetObjectItem(item, "timeout_ms")->valueint : 200;
        dev->status_tag_id = cJSON_GetObjectItem(item, "status_tag_id")->valueint;

        // TCP 专属解析
        cJSON *ip_item = cJSON_GetObjectItem(item, "target_ip");
        if (ip_item && dev->transport != 0) {
            sscanf(ip_item->valuestring, "%hhu.%hhu.%hhu.%hhu", &dev->target_ip[0], &dev->target_ip[1], &dev->target_ip[2], &dev->target_ip[3]);
            dev->target_port = cJSON_GetObjectItem(item, "target_port")->valueint;
        }

        // Modbus 参数解析 (容错处理)
        if (dev->protocol == PROTO_MODBUS_RTU || dev->protocol == PROTO_MODBUS_TCP) {
            dev->slave_id = cJSON_GetObjectItem(item, "slave_id") ? cJSON_GetObjectItem(item, "slave_id")->valueint : 1;
            dev->func_code = cJSON_GetObjectItem(item, "func_code") ? cJSON_GetObjectItem(item, "func_code")->valueint : 3;
            dev->start_reg = cJSON_GetObjectItem(item, "start_reg") ? cJSON_GetObjectItem(item, "start_reg")->valueint : 0;
            dev->reg_count = cJSON_GetObjectItem(item, "reg_count") ? cJSON_GetObjectItem(item, "reg_count")->valueint : 1;
        }

        // 非标协议自定义配置解析
        cJSON *custom_node = cJSON_GetObjectItem(item, "custom_cfg");
        if (custom_node) {
            cJSON *tx_hex = cJSON_GetObjectItem(custom_node, "tx_hex");
            if (tx_hex) dev->custom.tx_len = hex_str_to_bytes(tx_hex->valuestring, dev->custom.tx_payload, sizeof(dev->custom.tx_payload));
            
            cJSON *fm_node = cJSON_GetObjectItem(custom_node, "frame_mode");
            if (fm_node && strcmp(fm_node->valuestring, "HEAD_TAIL") == 0) dev->custom.frame_mode = MODE_HEAD_TAIL;
            else if (fm_node && strcmp(fm_node->valuestring, "HEAD_FIXED") == 0) dev->custom.frame_mode = MODE_HEAD_FIXED;
            
            cJSON *head_node = cJSON_GetObjectItem(custom_node, "header");
            if (head_node) dev->custom.header_len = hex_str_to_bytes(head_node->valuestring, dev->custom.header, sizeof(dev->custom.header));
            
            cJSON *foot_node = cJSON_GetObjectItem(custom_node, "footer");
            if (foot_node) dev->custom.footer_len = hex_str_to_bytes(foot_node->valuestring, dev->custom.footer, sizeof(dev->custom.footer));
        }

        reg_map_add_tag(dev->status_tag_id, dev->name, TAG_TYPE_BOOL, false);

        // 测点规则映射
        cJSON *rules_arr = cJSON_GetObjectItem(item, "rules");
        int rule_cnt = cJSON_GetArraySize(rules_arr);
        prof->mapping_rules = calloc(rule_cnt, sizeof(modbus_mapping_rule_t));
        prof->rule_count = rule_cnt;
        prof->slave_id = dev->slave_id;

        for (int j = 0; j < rule_cnt; j++) {
            cJSON *r_item = cJSON_GetArrayItem(rules_arr, j);
            modbus_mapping_rule_t *rule = &prof->mapping_rules[j];
            rule->target_tag_id = cJSON_GetObjectItem(r_item, "tag_id")->valueint;
            rule->byte_offset = cJSON_GetObjectItem(r_item, "offset")->valueint;
            rule->type = cJSON_GetObjectItem(r_item, "type")->valueint;
            rule->scale = cJSON_GetObjectItem(r_item, "scale")->valuedouble;
            
            cJSON *bit_item = cJSON_GetObjectItem(r_item, "bit");
            if (bit_item) rule->bit_offset = bit_item->valueint;

            const char* tag_name = cJSON_GetObjectItem(r_item, "name")->valuestring;
            tag_data_type_t rtdb_type = (rule->type >= MB_TYPE_FLOAT32_ABCD) ? TAG_TYPE_FLOAT32 : TAG_TYPE_INT32;
            reg_map_add_tag(rule->target_tag_id, tag_name, rtdb_type, false);
        }

        dev->base_tag_id = i; 
        dev->parse_func = generic_parser_wrapper; // 暂留，后续阶段解耦分发
    }

    cJSON_Delete(root);
    *out_sensors = dev_array;
    *out_count = sensor_cnt;
    ESP_LOGI(TAG, "Successfully loaded V2.0 config: GW_ID=%s, Nodes=%d", g_gw_config.device_id, sensor_cnt);
    return true;
}

char* config_manager_get_json(void) {
    return bsp_fs_read_file_to_str(CONFIG_FILE_PATH);
}

bool config_manager_save_json(const char *json_str) {
    cJSON *test = cJSON_Parse(json_str);
    if (!test) return false;
    cJSON_Delete(test);
    return bsp_fs_write_str_to_file(CONFIG_FILE_PATH, json_str) == ESP_OK;
}