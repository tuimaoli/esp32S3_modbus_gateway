/**
 * @file modbus_template.h
 * @brief 中间件层：Modbus 数据驱动模板与字节抽取规则定义
 * @note 用于取代硬编码的解析函数，实现基于配置的动态数据萃取
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Modbus 数据类型及字节序 (Endianness) 定义
 * @note 解决不同厂家浮点数、32位整数大小端不一致的痛点
 */
typedef enum {
    MB_TYPE_BOOL = 0,       ///< 提取单个 Bit (需要 bit_offset)
    MB_TYPE_INT16_AB,       ///< 16位有符号整数 (大端，标准Modbus)
    MB_TYPE_UINT16_AB,      ///< 16位无符号整数 (大端)
    MB_TYPE_INT16_BA,       ///< 16位有符号整数 (小端/字节交换)
    
    MB_TYPE_INT32_ABCD,     ///< 32位整数 (大端)
    MB_TYPE_INT32_CDAB,     ///< 32位整数 (字交换，最常见)
    MB_TYPE_INT32_DCBA,     ///< 32位整数 (小端)
    
    MB_TYPE_FLOAT32_ABCD,   ///< 标准 IEEE754 单精度浮点 (大端)
    MB_TYPE_FLOAT32_CDAB,   ///< 浮点数字交换 (工业界极为常见)
    MB_TYPE_FLOAT32_DCBA    ///< 浮点数 (小端)
} modbus_data_type_e;

/**
 * @brief 单个测点的映射规则 (Mapping Rule)
 * @note 相当于上位机 Excel 表格中的“一行”配置
 */
typedef struct {
    char name[32];          ///< 测点名称，供 MQTT 拼装 JSON 键值对使用
    uint16_t target_tag_id; ///< 解析后要存入的 RTDB 逻辑 ID
    uint16_t byte_offset;   ///< 在原始 Payload 中的字节偏移量 (例如: 从第 4 字节开始取)
    uint8_t  bit_offset;    ///< 位偏移量 0~15 (仅当类型为 MB_TYPE_BOOL 时有效)
    modbus_data_type_e type;///< 数据类型及字节序解码方式
    float    scale;         ///< 缩放系数 (例如: 读到 123, scale=0.1, 存入RTDB为 12.3)
} modbus_mapping_rule_t;

/**
 * @brief 动态传感器设备画像 (Device Profile)
 * @note 替代原有的固定结构体，内部持有动态分配的规则表
 */
typedef struct {
    uint8_t slave_id;       ///< 从站 ID
    modbus_mapping_rule_t *mapping_rules; ///< 动态数组指针：存放所有测点的抽取规则
    int rule_count;         ///< 这个传感器包含了多少个映射测点
} sensor_profile_t;

/**
 * @brief 通用负载解析引擎 (核心)
 * @param rx_payload Modbus 响应报文的数据部分 (跳过了 SlaveID、Func 和 ByteCount)
 * @param payload_len 纯数据部分的字节长度
 * @param profile 该传感器对应的画像规则
 */
void modbus_universal_parser(const uint8_t *rx_payload, uint16_t payload_len, const sensor_profile_t *profile);