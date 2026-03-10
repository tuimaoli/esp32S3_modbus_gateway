/**
 * @file modbus_template.h
 * @brief 中间件层：协议模型与数据字典映射模板
 * @note 承载了所有多态设备的底层数据结构，供上下游模块统一引用
 */
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** * @brief 数据类型枚举 (包含各种字节序) 
 */
enum {
    MB_TYPE_UINT16_AB    = 1,   ///< 16位无符号整数 (大端，标准Modbus)
    MB_TYPE_UINT16_BA    = 2,   ///< 16位无符号整数 (小端)
    MB_TYPE_FLOAT32_ABCD = 7,   ///< 32位标准浮点数 (大端)
    MB_TYPE_FLOAT32_CDAB = 8,   ///< 32位字反转浮点数
    MB_TYPE_BOOL         = 9,   ///< 布尔开关量
    MB_TYPE_UINT8        = 10,  ///< 8位单字节
    MB_TYPE_FLOAT32_DCBA = 11,  ///< 32位浮点数 (小端)
    MB_TYPE_FLOAT32_BADC = 12   ///< 32位字节反转浮点数
};

/** * @brief 泛化协议枚举 
 */
typedef enum {
    PROTO_MODBUS_RTU    = 0,    ///< 标准 Modbus RTU 串口协议
    PROTO_MODBUS_TCP    = 1,    ///< 标准 Modbus TCP 网络协议
    PROTO_CUSTOM_POLL   = 2,    ///< 非标协议：一问一答型 (网关主动查询)
    PROTO_CUSTOM_REPORT = 3     ///< 非标协议：主动上报型 (网关仅被动监听)
} protocol_type_e;

/** * @brief 非标协议分帧模式枚举 
 */
typedef enum {
    MODE_HEAD_TAIL  = 0,        ///< 依赖特定的帧头和帧尾截断
    MODE_HEAD_LEN   = 1,        ///< 依赖帧头和报文内部的长度字段截断
    MODE_HEAD_FIXED = 2         ///< 依赖帧头和固定的总长度截断
} frame_mode_e;

/** * @brief 单个测点的萃取规则 
 */
typedef struct {
    char name[32];              ///< 测点字符串名称 (供 MQTT 序列化为 JSON 键名)
    uint16_t target_tag_id;     ///< 映射到本地实时数据库 (RTDB) 的目标点位 ID
    uint16_t byte_offset;       ///< 该数据在原始数据帧中的起始字节偏移量
    uint8_t bit_offset;         ///< 位偏移量 (针对按位读取的布尔量)
    uint8_t type;               ///< 数据类型与字节序枚举 (如 MB_TYPE_FLOAT32_ABCD)
    float scale;                ///< 缩放因子 (如 0.1 表示将读取的值除以 10)
} modbus_mapping_rule_t;

/** * @brief 多态传感器设备画像 (核心总类) 
 */
typedef struct sensor_device {
    char name[32];              ///< 传感器设备名称
    uint8_t transport;          ///< 物理链路选择 (0:RS485, 1:W5100S)
    protocol_type_e protocol;   ///< 协议类型选择
    
    uint32_t poll_interval_ms;  ///< 轮询时间间隔 (毫秒)
    uint32_t timeout_ms;        ///< 等待数据回传的超时时间 (毫秒)
    uint8_t  retry_count;       ///< 失败重试次数
    
    uint8_t target_ip[4];       ///< 目标设备的 IP 地址 (TCP 协议用)
    uint16_t target_port;       ///< 目标设备的端口号 (TCP 协议用)
    uint8_t slave_id;           ///< 从站地址 / 设备地址
    uint8_t func_code;          ///< 协议功能码 (如 Modbus 0x03)
    uint16_t start_reg;         ///< 批量读取的起始寄存器地址
    uint16_t reg_count;         ///< 批量读取的寄存器总数
    
    struct {
        uint8_t tx_payload[64]; ///< 非标协议预编译的查询指令流
        uint16_t tx_len;        ///< 查询指令的实际长度
        frame_mode_e frame_mode;///< 粘包/半包处理的断帧模式
        uint8_t header[8];      ///< 非标协议帧头特征字节
        uint8_t header_len;     ///< 帧头特征字节长度
        uint8_t footer[8];      ///< 非标协议帧尾特征字节
        uint8_t footer_len;     ///< 帧尾特征字节长度
        int fixed_len;          ///< 规定的一帧固定总长度 (定长模式使用)
    } custom;

    uint16_t base_tag_id;       ///< 基础预留标签 ID
    uint16_t status_tag_id;     ///< 在线状态存储点位 ID (1.0表示在线，0.0表示离线)
    modbus_mapping_rule_t *rules; ///< 动态分配的切片规则数组指针
    int rule_count;             ///< 该设备包含的切片规则总数

    void (*parse_func)(const uint8_t *rx_buf, uint16_t len, uint16_t base_tag_id); ///< 多态解析函数指针
} sensor_device_t;

/** * @brief 遗留结构体 (用于向下兼容旧的 parser 函数) 
 */
typedef struct {
    uint8_t slave_id;                       ///< 从站 ID
    modbus_mapping_rule_t *mapping_rules;   ///< 规则映射表
    int rule_count;                         ///< 规则数量
} sensor_profile_t;

void modbus_universal_parser(const uint8_t *rx_data, uint16_t data_len, const sensor_profile_t *profile);

#ifdef __cplusplus
}
#endif