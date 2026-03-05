/**
 * @file config_manager.c
 * @brief 应用层：配置管理器实现 (带出厂默认值自动生成)
 */
#include "config_manager.h"
#include "bsp_fs.h"
#include "register_map.h"
#include "modbus_template.h"
#include "cJSON.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "CONFIG_MGR";
#define CONFIG_FILE_PATH "/vfs/sensors.json"

/* ============================================================
 * 🚨 核心亮点：出厂默认 JSON 模板 (Factory Default)
 * 当系统第一次烧录或格式化后，会自动将此模板写入 LittleFS
 * ============================================================ */
static const char* DEFAULT_JSON_CONFIG = 
"{\n"
"  \"version\": \"1.0\",\n"
"  \"sensors\": [\n"
"    {\n"
"      \"name\": \"Room1_TH_Sensor\",\n"
"      \"transport\": 0,\n"          // 0: RS485 RTU
"      \"slave_id\": 1,\n"
"      \"func_code\": 3,\n"
"      \"start_reg\": 0,\n"
"      \"reg_count\": 2,\n"
"      \"status_tag_id\": 199,\n"
"      \"rules\": [\n"
"        { \"name\": \"Room1_Temp\", \"tag_id\": 100, \"offset\": 0, \"type\": 1, \"scale\": 0.1 },\n"
"        { \"name\": \"Room1_Humi\", \"tag_id\": 101, \"offset\": 2, \"type\": 1, \"scale\": 0.1 }\n"
"      ]\n"
"    },\n"
"    {\n"
"      \"name\": \"Smart_Meter_TCP\",\n"
"      \"transport\": 1,\n"          // 1: W5100S 硬件 TCP
"      \"target_ip\": \"192.168.1.100\",\n"
"      \"target_port\": 502,\n"
"      \"slave_id\": 1,\n"
"      \"func_code\": 3,\n"
"      \"start_reg\": 256,\n"
"      \"reg_count\": 2,\n"
"      \"status_tag_id\": 200,\n"
"      \"rules\": [\n"
"        { \"name\": \"Phase_A_Volt\", \"tag_id\": 201, \"offset\": 0, \"type\": 8, \"scale\": 1.0 }\n" // 8: MB_TYPE_FLOAT32_CDAB
"      ]\n"
"    }\n"
"  ]\n"
"}";

static sensor_profile_t *g_dynamic_profiles = NULL;
static int g_profile_count = 0;

static void generic_parser_wrapper(const uint8_t *rx_buf, uint16_t len, uint16_t profile_index) {
    if (len < 5 || profile_index >= g_profile_count) return;
    uint8_t byte_count = rx_buf[2];
    if (len < (3 + byte_count)) return;
    modbus_universal_parser(&rx_buf[3], byte_count, &g_dynamic_profiles[profile_index]);
}

bool config_manager_load(sensor_device_t **out_sensors, int *out_count) {
    char *json_str = bsp_fs_read_file_to_str(CONFIG_FILE_PATH);
    
    // 自动补全机制：如果没有文件，则写入默认配置
    if (!json_str) {
        ESP_LOGW(TAG, "Config file not found. Generating factory default...");
        bsp_fs_write_str_to_file(CONFIG_FILE_PATH, DEFAULT_JSON_CONFIG);
        json_str = strdup(DEFAULT_JSON_CONFIG); 
    }

    cJSON *root = cJSON_Parse(json_str);
    free(json_str);
    if (!root) {
        ESP_LOGE(TAG, "JSON parse error!");
        return false;
    }

    cJSON *sensors_arr = cJSON_GetObjectItem(root, "sensors");
    if (!cJSON_IsArray(sensors_arr)) {
        cJSON_Delete(root);
        return false;
    }

    int sensor_cnt = cJSON_GetArraySize(sensors_arr);
    if (sensor_cnt == 0) {
        cJSON_Delete(root);
        *out_sensors = NULL;
        *out_count = 0;
        return true;
    }

    sensor_device_t *dev_array = calloc(sensor_cnt, sizeof(sensor_device_t));
    g_dynamic_profiles = calloc(sensor_cnt, sizeof(sensor_profile_t));
    g_profile_count = sensor_cnt;

    for (int i = 0; i < sensor_cnt; i++) {
        cJSON *item = cJSON_GetArrayItem(sensors_arr, i);
        sensor_device_t *dev = &dev_array[i];
        sensor_profile_t *prof = &g_dynamic_profiles[i];

        strncpy(dev->name, cJSON_GetObjectItem(item, "name")->valuestring, 31);
        dev->transport = cJSON_GetObjectItem(item, "transport")->valueint;
        dev->slave_id = cJSON_GetObjectItem(item, "slave_id")->valueint;
        dev->func_code = cJSON_GetObjectItem(item, "func_code")->valueint;
        dev->start_reg = cJSON_GetObjectItem(item, "start_reg")->valueint;
        dev->reg_count = cJSON_GetObjectItem(item, "reg_count")->valueint;
        dev->status_tag_id = cJSON_GetObjectItem(item, "status_tag_id")->valueint;
        
        cJSON *ip_item = cJSON_GetObjectItem(item, "target_ip");
        if (ip_item && dev->transport != MB_TRANSPORT_RTU) {
            sscanf(ip_item->valuestring, "%hhu.%hhu.%hhu.%hhu", 
                   &dev->target_ip[0], &dev->target_ip[1], &dev->target_ip[2], &dev->target_ip[3]);
            dev->target_port = cJSON_GetObjectItem(item, "target_port")->valueint;
        }

        reg_map_add_tag(dev->status_tag_id, dev->name, TAG_TYPE_BOOL, false);

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
            if (rule->type == MB_TYPE_BOOL) rtdb_type = TAG_TYPE_BOOL;
            
            reg_map_add_tag(rule->target_tag_id, tag_name, rtdb_type, false);
        }

        dev->base_tag_id = i; 
        dev->parse_func = generic_parser_wrapper;
    }

    cJSON_Delete(root);
    *out_sensors = dev_array;
    *out_count = sensor_cnt;
    ESP_LOGI(TAG, "Successfully loaded %d sensors.", sensor_cnt);
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