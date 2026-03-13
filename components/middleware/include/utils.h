/**
 * @file utils.h
 * @brief 中间件层：通用核心工具库 (JSON校验、CRC算法、字符串处理等)
 * @note 全局高频调用的核心算法库
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * 1. 工业通信算法 (高频内联宏)
 * ==========================================*/

/**
 * @brief 标准 Modbus CRC16 快速校验算法
 * @note 采用 static inline 保证类型安全且免去函数调用压栈开销，等效于完美宏展开
 */
static inline uint16_t utils_crc16_modbus(const uint8_t *buf, uint16_t len) {
    uint16_t crc = 0xFFFF;
    for (uint16_t pos = 0; pos < len; pos++) {
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

/// @brief 快捷调用宏：自动处理指针强转
#define UTILS_CRC16(buf, len) utils_crc16_modbus((const uint8_t*)(buf), (uint16_t)(len))


/* ============================================================
 * 2. 组态防呆校验机制
 * ==========================================*/

/**
 * @brief 严格校验网关 JSON 组态配置的合法性
 * @param json_str 原始 JSON 字符串
 * @param err_buf 错误信息回传缓冲区
 * @param err_len 缓冲区大小
 * @return true: 校验通过; false: 格式错误或缺失关键节点
 */
bool utils_validate_gateway_config(const char *json_str, char *err_buf, size_t err_len);


#ifdef __cplusplus
}
#endif