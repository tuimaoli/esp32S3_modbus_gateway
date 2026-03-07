/**
 * @file config_manager.h
 * @brief 应用层：动态组态配置管理器 V2.0
 * @note 引入全局网关配置与非标协议泛化支持
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "modbus_template.h"

#ifdef __cplusplus
extern "C" {
#endif

// 协议多态枚举
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
// V2.0 新增：网关全局配置
// ==========================================
typedef struct {
    char device_id[32];
    char mqtt_broker[64];
    char mqtt_user[32];
    char mqtt_pass[64];
    char mqtt_sub_topics[4][64]; // 最多支持4个动态订阅主题
    int  sub_topic_count;
    uint32_t upload_interval_ms;
} gateway_config_t;

// ==========================================
// V2.0 升级：多态传感器设备画像
// ==========================================
typedef struct sensor_device {
    char name[32];              
    uint8_t transport;           // 物理层: 0-RS485, 1-W5100S, 2-WiFi
    protocol_type_e protocol;    // 协议层类型
    
    // --- 高级调度参数 ---
    uint32_t poll_interval_ms;
    uint32_t timeout_ms;
    uint8_t  retry_count;
    
    // --- Modbus 标准参数 (保留) ---
    uint8_t target_ip[4];       
    uint16_t target_port;       
    uint8_t slave_id;           
    uint8_t func_code;          
    uint16_t start_reg;         
    uint16_t reg_count;         
    
    // --- 非标自定义协议参数 (新增) ---
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

    // 预留多态解析函数指针 (为下一阶段的协议分离做准备)
    void (*parse_func)(const uint8_t *rx_buf, uint16_t len, uint16_t base_tag_id);
} sensor_device_t;

/**
 * @brief 获取网关全局配置参数
 */
const gateway_config_t* config_manager_get_gw_cfg(void);

bool config_manager_load(sensor_device_t **out_sensors, int *out_count);
char* config_manager_get_json(void);
bool config_manager_save_json(const char *json_str);

#ifdef __cplusplus
}
#endif