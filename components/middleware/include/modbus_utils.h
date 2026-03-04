/**
 * @file modbus_utils.h
 * @brief 中间件层：Modbus 基础算法支持库
 * @note 采用 static inline 方式，避免多重定义问题，提升执行效率
 */
#pragma once

#include <stdint.h>

/**
 * @brief 计算 Modbus 协议标准的 CRC16 校验码
 * @param buf 待计算的数据缓冲区指针
 * @param len 待计算的数据长度 (字节数)
 * @return 16 位的 CRC 校验结果 (低字节在前，高字节在后)
 */
static inline uint16_t modbus_crc16(const uint8_t *buf, uint16_t len) {
    uint16_t crc = 0xFFFF;
    for (int pos = 0; pos < len; pos++) {
        crc ^= (uint16_t)buf[pos];
        for (int i = 8; i != 0; i--) {
            if ((crc & 0x0001) != 0) { 
                crc >>= 1; 
                crc ^= 0xA001; 
            } else { 
                crc >>= 1; 
            }
        }
    }
    return crc;
}