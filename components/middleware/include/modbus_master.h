/**
 * @file modbus_master.h
 * @brief 中间件层：Modbus RTU 主机引擎接口
 * @note 负责轮询 485 总线上的从机设备，并调用绑定的解析器清洗数据
 */
#pragma once
#include <stdint.h>

/**
 * @brief 传感器解析回调函数指针类型
 * @param rx_buf      接收到的 Modbus 原始数据 (包含从机地址等完整报文)
 * @param len         数据长度
 * @param base_tag_id 该传感器数据映射到本地 RTDB 的起始标签 ID
 */
typedef void (*sensor_parse_cb_t)(const uint8_t *rx_buf, uint16_t len, uint16_t base_tag_id);

/**
 * @brief 传感器/从机对象描述符
 * @note  增加新设备时，只需在 App 层实例化这个结构体，无需修改底层轮询逻辑
 */
typedef struct {
    const char* name;             ///< 设备名称 (便于调试日志输出)
    uint8_t slave_id;             ///< 目标 Modbus 从机地址 (1~247)
    uint8_t func_code;            ///< 读取功能码 (如 0x03 读保持寄存器, 0x04 读输入寄存器)
    uint16_t start_reg;           ///< 目标设备寄存器起始地址
    uint16_t reg_count;           ///< 期望读取的连续寄存器数量
    uint16_t base_tag_id;         ///< 映射到本机 RTDB 的起始标签 ID (测点地址)
    uint16_t status_tag_id;       ///< 设备在线状态专属映射 ID (1=在线, 0=离线，不使用填 0)
    sensor_parse_cb_t parse_func; ///< 绑定的解析回调函数 (负责数据清洗入库)
} sensor_device_t;

/**
 * @brief 初始化 Modbus Master 核心引擎
 * @param uart_port BSP 层的 UART 端口号 (如 1 或 2)
 */
void modbus_master_init(int uart_port);

/**
 * @brief 执行一次轮询周期 (扫描所有注册的传感器)
 * @param sensors 传感器对象配置数组首地址
 * @param count   传感器数量
 */
void modbus_master_poll_cycle(const sensor_device_t *sensors, int count);