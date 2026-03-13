/**
 * @file modbus_template.h
 * @brief 中间件层：动态模板映射与多态数据类型字典 (V4.0)
 * @note 统一定义工业现场设备的报文解析规则与反向控制属性
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 工业现场多态数据类型字典 (解决大小端与字节序乱象)
 * @note AB代表大端(Big-Endian), BA代表小端, CDAB代表字反序(Word-Swapped)
 */
typedef enum {
    MB_TYPE_UINT16_AB = 0,   ///< 16位无符号整数 (标准 Modbus)
    MB_TYPE_UINT16_BA,       ///< 16位无符号整数 (小端)
    MB_TYPE_INT16_AB,        ///< 16位有符号整数 (标准)
    MB_TYPE_INT16_BA,        ///< 16位有符号整数 (小端)
    
    MB_TYPE_UINT32_ABCD,     ///< 32位无符号整数 (大端)
    MB_TYPE_UINT32_CDAB,     ///< 32位无符号整数 (字反序，西门子/施耐德常用)
    MB_TYPE_UINT32_BADC,     ///< 32位无符号整数 (字节反序)
    MB_TYPE_UINT32_DCBA,     ///< 32位无符号整数 (小端)
    
    MB_TYPE_INT32_ABCD,      ///< 32位有符号整数 (大端)
    MB_TYPE_INT32_CDAB,      ///< 32位有符号整数 (字反序)
    
    MB_TYPE_FLOAT32_ABCD,    ///< 32位 IEEE754 浮点数 (大端)
    MB_TYPE_FLOAT32_CDAB,    ///< 32位 IEEE754 浮点数 (字反序)
    MB_TYPE_FLOAT32_BADC,    ///< 32位 IEEE754 浮点数 (字节反序)
    MB_TYPE_FLOAT32_DCBA     ///< 32位 IEEE754 浮点数 (小端)
} modbus_data_type_e;

/**
 * @brief 单个测点的解析映射与控制规则模型
 */
typedef struct {
    char     name[32];          ///< 测点业务名称 (如 "temperature")
    uint16_t target_tag_id;     ///< 解析后抛给 RTDB 的目标虚拟逻辑 ID
    
    /* * 架构魔法：使用 C11 匿名联合体 
     * 完美兼容 ConfigManager(使用 offset) 和 ProtocolEngine(使用 byte_offset) 的不同命名习惯
     */
    union {
        uint16_t offset;
        uint16_t byte_offset;   ///< 在原始报文 Payload 中的字节偏移量
    };
    
    uint8_t  type;              ///< 数据类型 (对应 modbus_data_type_e 枚举)
    uint8_t  bit;               ///< 位偏移 (暂预留，用于后续按位提取开关量)
    float    scale;             ///< 缩放因子 (如 0.1 表示将读数除以 10)
    
    /* ============================================================
     * V4.0 边缘联动与反向控制高级属性
     * ============================================================ */
    bool     writable;          ///< 是否允许北向或边缘联动引擎覆盖写入
    bool     persist;           ///< 是否开启 NVS 掉电记忆保护
    uint16_t reverse_reg;       ///< 反向控制映射：向物理从机下发写指令的目标寄存器地址 (0xFFFF 视为无物理映射)
    
} modbus_mapping_rule_t;

#ifdef __cplusplus
}
#endif