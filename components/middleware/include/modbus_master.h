/**
 * @file modbus_master.h
 * @brief 中间件层：Modbus 轮询引擎接口 (支持 RTU, 硬件 TCP, Wi-Fi 软 TCP)
 */
#pragma once
#include <stdint.h>
#include "modbus_template.h"

/** @brief 定义物理传输链路类型 */
typedef enum {
    MB_TRANSPORT_RTU = 0,         ///< 传统 RS485 串口物理层
    MB_TRANSPORT_TCP_W5100S,      ///< 以太网 W5100S 硬件 Socket 层
    MB_TRANSPORT_TCP_WIFI         ///< 厂区 Wi-Fi LwIP 软 Socket 层
} mb_transport_e;

/**
 * @brief 动态传感器设备画像 (三模态融合)
 */
typedef struct sensor_device {
    char name[32];              
    mb_transport_e transport;   ///< 传输链路选择
    
    // ======== TCP/Wi-Fi 专属配置 ========
    uint8_t target_ip[4];       
    uint16_t target_port;       
    
    // ======== RTU / 通用配置 ========
    uint8_t slave_id;           
    uint8_t func_code;          
    uint16_t start_reg;         
    uint16_t reg_count;         
    
    uint16_t base_tag_id;       
    uint16_t status_tag_id;     
    
    void (*parse_func)(const uint8_t *rx_buf, uint16_t len, uint16_t base_tag_id);
} sensor_device_t;

void modbus_master_init(int uart_port);
void modbus_master_poll_cycle(const sensor_device_t *sensors, int count);