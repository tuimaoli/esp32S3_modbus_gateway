/**
 * @file protocol_engine.h
 * @brief 中间件层：泛化协议调度引擎 (V4.2 SDH 软件定义硬件终极版)
 * @note 支持面向对象 (OOP) 的多物理串口并行调度，以及独立的 TCP/Wi-Fi 网络设备轮询
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "modbus_template.h"
#include "bsp_serial_port.h" // 引入 OOP 串口句柄，彻底与原生硬件解耦

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
// V4.2 升级：多态传感器设备画像 (核心数据模型)
// ==========================================
typedef struct sensor_device {
    char name[32];              
    uint8_t transport;           ///< 物理层: 0-RS485本地串口, 1-W5100S, 2-WiFi
    int bind_port_id;            ///< ⚡ 核心修复：绑定的逻辑串口 ID (例如指向 Port 1 或 Port 2)
    
    // 网络设备专用
    uint8_t target_ip[4];        ///< 目标设备的 IP 地址
    uint16_t target_port;        ///< 目标设备的端口号 (如 502)

    protocol_type_e protocol;
    int poll_interval_ms;
    int timeout_ms;
    
    uint8_t slave_id;
    uint8_t func_code;
    uint16_t start_reg;
    uint16_t reg_count;

    struct {
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
 * @brief 向发送队列压入反向控制指令 (供 RTDB 或 云端调用)
 * @param slave_id 目标从机地址
 * @param reg_addr 目标寄存器地址
 * @param value 写入的数值
 * @return 压入队列是否成功
 */
bool protocol_engine_push_tx_queue(uint8_t slave_id, uint16_t reg_addr, float value);

/* ============================================================
 * V4.2 核心多态轮询入口：彻底分离串口总线与网络总线
 * ============================================================ */

/**
 * @brief [多线程并行] 执行一次面向对象 (OOP) 的物理串口轮询周期
 * @param port 面向对象的物理/拓展串口句柄 (如原生 UART 或 SC16IS750)
 * @param sensors 挂载在该串口上的传感器组态数组
 * @param count 挂载节点的数量
 */
void protocol_engine_poll_serial_cycle(bsp_serial_port_t *port, sensor_device_t *sensors, int count);

/**
 * @brief [独立线程] 执行一次网络层 (硬件 TCP/软 Wi-Fi) 的设备轮询周期
 * @param sensors 所有被标记为 transport=1 或 2 的网络传感器数组
 * @param count 网络传感器的数量
 */
void protocol_engine_poll_network_cycle(sensor_device_t *sensors, int count);

#ifdef __cplusplus
}
#endif