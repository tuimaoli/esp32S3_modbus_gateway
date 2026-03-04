/**
 * @file sensor_parsers.c
 * @brief 中间件层：各种具体的传感器解析逻辑实现 (数据清洗工厂)
 * @note 负责将 Modbus 原始报文转化为有实际物理意义的数据，并推送到 RTDB 数据中枢
 */

#include "sensor_parsers.h"
#include "modbus_master.h"
#include "register_map.h"
#include "esp_log.h"

static const char *TAG = "Parsers";

/**
 * @brief 预留：动态解析器路由字典 (工厂模式映射用)
 * @note 未来可通过 JSON 配置文件中的字符串，动态绑定下方对应的解析函数
 */
typedef enum {
    STANDARD_PARSER = 0,
    SPECIAL_SENSOR_PARSER
} sensor_parser_type_e;

const char *g_parser_array[] = {
    "parser_standard_u16",
    "parser_special_temp_sensor"
};

/* ============================================================
 * 标准解析器 (直接拷贝数据)
 * 适用于：从机数据格式和本机定义一致，无需复杂转换的 U16 寄存器
 * ============================================================ */
void parser_standard_u16(const uint8_t *rx_buf, uint16_t len, uint16_t base_tag_id)
{
    // 最小有效 Modbus 响应报文长度防错检查
    // 格式: [SlaveID] [Func] [ByteCount] [DataHi] [DataLo] ... [CRCLo] [CRCHi]
    if (len < 5) return; 
    
    uint8_t byte_count = rx_buf[2];
    int reg_qty = byte_count / 2;
    
    for (int i = 0; i < reg_qty; i++) {
        // Modbus 是大端，ESP32 是小端，提取并组合
        uint16_t raw_val = (rx_buf[3 + i * 2] << 8) | rx_buf[4 + i * 2];
        
        // 写入 RTDB 数据中心 (会自动更新时间戳和 GOOD 质量戳)
        reg_map_update_value(base_tag_id + i, (float)raw_val);
    }
}

/* ============================================================
 * 特殊传感器解析 (如：需要跨寄存器计算或合并字节)
 * 假设某温湿度传感器：
 * 返回 2 个寄存器，但需要 (Reg[0] * 10) + (Reg[1] / 10) 才能得到真实值
 * ============================================================ */
void parser_special_temp_sensor(const uint8_t *rx_buf, uint16_t len, uint16_t base_tag_id)
{
    // 需要至少 2 个寄存器的数据 (4 bytes) + 头尾 (至少 7 bytes)
    if (len < 7) return; 

    uint16_t raw_val_1 = (rx_buf[3] << 8) | rx_buf[4];
    uint16_t raw_val_2 = (rx_buf[5] << 8) | rx_buf[6];

    // 进行特殊的业务计算
    uint32_t calc_val = (raw_val_1 * 10) + (raw_val_2 / 10);
    
    ESP_LOGI(TAG, "Special Sensor Parsed Result: %lu", calc_val);

    // 【方案 A: 严格模拟原有双寄存器存放形式】
    uint16_t res_hi = (calc_val >> 16) & 0xFFFF;
    uint16_t res_lo = calc_val & 0xFFFF;
    reg_map_update_value(base_tag_id, (float)res_hi);
    reg_map_update_value(base_tag_id + 1, (float)res_lo);

    /* // 【方案 B: RTDB 高级用法 (推荐)】
    // 既然 RTDB 内部统一使用 float 存储物理量，对于拼接好的 32 位真实物理量，
    // 直接存入 1 个 Tag 更加优雅，上位机直接读 Float 即可，无需再手动拼接：
    // reg_map_update_value(base_tag_id, (float)calc_val);
    */
}