/**
 * @file modbus_template.c
 * @brief 中间件层：基于映射规则的通用 Modbus 数据抽取引擎实现
 */

#include "modbus_template.h"
#include "register_map.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "UNV_PARSER";

/**
 * @brief 根据类型和字节序，从字节流中解码 32 位数据 (含 Float)
 */
static float decode_32bit_value(const uint8_t *data, modbus_data_type_e type) 
{
    union {
        uint32_t u32;
        int32_t  i32;
        float    f32;
    } val;

    // 根据工业常见的字节序进行拼装
    switch (type) {
        case MB_TYPE_INT32_ABCD:
        case MB_TYPE_FLOAT32_ABCD:
            val.u32 = (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];
            break;
            
        case MB_TYPE_INT32_CDAB:
        case MB_TYPE_FLOAT32_CDAB: // 最常见：字交换
            val.u32 = (data[2] << 24) | (data[3] << 16) | (data[0] << 8) | data[1];
            break;
            
        case MB_TYPE_INT32_DCBA:
        case MB_TYPE_FLOAT32_DCBA: // 纯小端
            val.u32 = (data[3] << 24) | (data[2] << 16) | (data[1] << 8) | data[0];
            break;
            
        default:
            val.u32 = 0;
            break;
    }

    // 强制类型双关 (Type Punning) 返回 float，适配 RTDB 统一入口
    if (type >= MB_TYPE_FLOAT32_ABCD) {
        return val.f32;
    } else {
        return (float)val.i32;
    }
}

void modbus_universal_parser(const uint8_t *rx_payload, uint16_t payload_len, const sensor_profile_t *profile)
{
    if (!profile || !profile->mapping_rules) return;

    // 遍历该传感器的所有提取规则 (类似 Excel 的每一行)
    for (uint16_t i = 0; i < profile->rule_count; i++) {
        modbus_mapping_rule_t *rule = &profile->mapping_rules[i];
        
        // 防呆：防止配置的偏移量超出了本次读取的有效负载
        if (rule->byte_offset >= payload_len) {
            ESP_LOGW(TAG, "Tag ID %d offset %d out of bounds!", rule->target_tag_id, rule->byte_offset);
            continue;
        }

        float final_val = 0.0f;
        const uint8_t *p_data = &rx_payload[rule->byte_offset];

        // 基于数据字典进行解析
        switch (rule->type) {
            case MB_TYPE_BOOL: {
                if (rule->byte_offset + 1 >= payload_len) break; // 需2个字节
                uint16_t reg_val = (p_data[0] << 8) | p_data[1];
                final_val = (reg_val & (1 << rule->bit_offset)) ? 1.0f : 0.0f;
                break;
            }
            case MB_TYPE_UINT16_AB: {
                if (rule->byte_offset + 1 >= payload_len) break;
                uint16_t val = (p_data[0] << 8) | p_data[1];
                final_val = (float)val;
                break;
            }
            case MB_TYPE_INT16_AB: {
                if (rule->byte_offset + 1 >= payload_len) break;
                int16_t val = (p_data[0] << 8) | p_data[1];
                final_val = (float)val;
                break;
            }
            case MB_TYPE_INT32_ABCD:
            case MB_TYPE_INT32_CDAB:
            case MB_TYPE_INT32_DCBA:
            case MB_TYPE_FLOAT32_ABCD:
            case MB_TYPE_FLOAT32_CDAB:
            case MB_TYPE_FLOAT32_DCBA: {
                if (rule->byte_offset + 3 >= payload_len) break; // 需4个字节
                final_val = decode_32bit_value(p_data, rule->type);
                break;
            }
            default:
                break;
        }

        // 缩放系数处理 (例如将读到的整数 254 乘以 0.1 变成真实的 25.4℃)
        if (rule->scale != 0.0f && rule->scale != 1.0f) {
            final_val *= rule->scale;
        }

        // 数据清洗和转换完毕，直接砸入内部 RTDB！(自动打上时间戳和 GOOD 质量戳)
        reg_map_update_value(rule->target_tag_id, final_val);
    }
}