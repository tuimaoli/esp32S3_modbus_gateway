/**
 * @file modbus_template.c
 * @brief 中间件层：遗留数据字典解析支持
 */
#include "modbus_template.h"
#include "register_map.h"
#include "esp_log.h"
#include <stddef.h>

/**
 * @brief 内部私有函数：处理 32 位数据的不同字节序
 */
static float decode_32bit_value(const uint8_t *p_data, uint8_t type) {
    uint32_t temp_val = 0;
    float final_val = 0.0f;
    
    switch (type) {
        case MB_TYPE_FLOAT32_ABCD:
            temp_val = (p_data[0] << 24) | (p_data[1] << 16) | (p_data[2] << 8) | p_data[3];
            final_val = *((float*)&temp_val);
            break;
        case MB_TYPE_FLOAT32_CDAB:
            temp_val = (p_data[2] << 24) | (p_data[3] << 16) | (p_data[0] << 8) | p_data[1];
            final_val = *((float*)&temp_val);
            break;
        case MB_TYPE_FLOAT32_DCBA:
            temp_val = (p_data[3] << 24) | (p_data[2] << 16) | (p_data[1] << 8) | p_data[0];
            final_val = *((float*)&temp_val);
            break;
        case MB_TYPE_FLOAT32_BADC:
            temp_val = (p_data[1] << 24) | (p_data[0] << 16) | (p_data[3] << 8) | p_data[2];
            final_val = *((float*)&temp_val);
            break;
        default:
            final_val = 0.0f;
            break;
    }
    
    return final_val;
}

void modbus_universal_parser(const uint8_t *rx_data, uint16_t data_len, const sensor_profile_t *profile) {
    if (profile == NULL || rx_data == NULL) {
        return;
    }

    for (int i = 0; i < profile->rule_count; i++) {
        const modbus_mapping_rule_t *rule = &profile->mapping_rules[i];
        
        if (rule->byte_offset + 1 > data_len) {
            continue;
        }

        const uint8_t *p = &rx_data[rule->byte_offset];
        float final_val = 0.0f;

        switch (rule->type) {
            case MB_TYPE_UINT16_AB:
                final_val = (float)((p[0] << 8) | p[1]);
                break;
            case MB_TYPE_UINT16_BA:
                final_val = (float)((p[1] << 8) | p[0]);
                break;
            case MB_TYPE_FLOAT32_ABCD:
            case MB_TYPE_FLOAT32_CDAB:
            case MB_TYPE_FLOAT32_DCBA:
            case MB_TYPE_FLOAT32_BADC:
                if (rule->byte_offset + 4 <= data_len) {
                    final_val = decode_32bit_value(p, rule->type);
                }
                break;
            default:
                final_val = (float)p[0];
                break;
        }

        final_val *= rule->scale;
        reg_map_update_value(rule->target_tag_id, final_val);
    }
}