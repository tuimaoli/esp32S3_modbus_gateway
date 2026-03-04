/**
 * @file sensor_parsers.h
 * @brief 中间件层：传感器协议清洗工厂
 */
#pragma once
#include <stdint.h>

/**
 * @brief 标准 16位无符号整数解析器
 * @param rx_buf 接收到的完整原始报文
 * @param len 报文长度
 * @param base_tag_id 映射的本地起始 ID
 */
void parser_standard_u16(const uint8_t *rx_buf, uint16_t len, uint16_t base_tag_id);

/**
 * @brief 特殊传感器解析器 (跨寄存器计算示例)
 * @param rx_buf 接收到的完整原始报文
 * @param len 报文长度
 * @param base_tag_id 映射的本地起始 ID
 */
void parser_special_temp_sensor(const uint8_t *rx_buf, uint16_t len, uint16_t base_tag_id);