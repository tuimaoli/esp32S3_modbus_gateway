/**
 * @file protocol_engine.h
 * @brief 中间件层：泛化协议调度引擎 (替代原 modbus_master)
 * @note 架构重构：引入数据抛出回调钩子，彻底与上层 MQTT 解耦
 */
#pragma once
#include <stdint.h>
#include "modbus_template.h"

#ifdef __cplusplus
extern "C" {
#endif

// 协议多态枚举 (下沉至中间件引擎层)
typedef enum {
    PROTO_MODBUS_RTU = 0,
    PROTO_MODBUS_TCP,
    PROTO_CUSTOM_POLL,   // 非标：一问一答型
    PROTO_CUSTOM_REPORT  // 非标：主动上报型
} protocol_type_e;

// 非标协议分帧模式枚举
typedef enum {
    MODE_HEAD_TAIL = 0,  // 靠 帧头+帧尾 截断
    MODE_HEAD_LEN,       // 靠 帧头+长度字节 截断
    MODE_HEAD_FIXED      // 靠 固定总长度 截断
} frame_mode_e;

// ==========================================
// V2.0 升级：多态传感器设备画像 (核心数据模型)
// ==========================================
typedef struct sensor_device {
    char name[32];              
    uint8_t transport;           // 物理层: 0-RS485, 1-W5100S, 2-WiFi
    protocol_type_e protocol;    // 协议层类型
    
    // --- 高级调度参数 ---
    uint32_t poll_interval_ms;
    uint32_t timeout_ms;
    uint8_t  retry_count;
    
    // --- Modbus 标准参数 ---
    uint8_t target_ip[4];       
    uint16_t target_port;       
    uint8_t slave_id;           
    uint8_t func_code;          
    uint16_t start_reg;         
    uint16_t reg_count;         
    
    // --- 非标自定义协议参数 ---
    struct {
        uint8_t tx_payload[64];  // 预编译的十六进制查询指令
        uint16_t tx_len;
        frame_mode_e frame_mode;
        uint8_t header[8];
        uint8_t header_len;
        uint8_t footer[8];
        uint8_t footer_len;
        int fixed_len;
    } custom;

    // --- 运行期状态与映射 ---
    uint16_t base_tag_id;       
    uint16_t status_tag_id;     
    modbus_mapping_rule_t *rules;
    int rule_count;

    // 预留多态解析函数指针
    void (*parse_func)(const uint8_t *rx_buf, uint16_t len, uint16_t base_tag_id);
} sensor_device_t;

/**
 * @brief 引擎向上层抛出解析完成数据的回调函数指针类型
 */
typedef void (*protocol_data_cb_t)(const char *sensor_name, const char *metric_name, float value);

/**
 * @brief 注册数据接收钩子 (解除上下层耦合的关键)
 */
void protocol_engine_register_data_cb(protocol_data_cb_t cb);

/**
 * @brief 初始化协议引擎 (分配硬件资源与底层队列)
 * @param uart_port RS485 绑定的 UART 端口号
 */
void protocol_engine_init(int uart_port);

/**
 * @brief 核心调度周期函数 (由 Gateway 任务循环调用)
 * @param sensors 动态传感器组态数组
 * @param count 传感器节点数量
 */
void protocol_engine_poll_cycle(const sensor_device_t *sensors, int count);

#ifdef __cplusplus
}
#endif