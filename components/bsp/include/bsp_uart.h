/**
 * @file bsp_uart.h
 * @brief 硬件抽象层：UART/RS485 接口
 * @note  Middleware 层只包含此头文件，不知道 ESP-IDF 驱动的存在
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// 定义端口句柄类型，对外隐藏 int 或 uart_port_t
typedef int bsp_uart_port_t; 

/**
 * @brief RS485 初始化配置结构体
 */
typedef struct {
    int tx_io_num;
    int rx_io_num;
    int rts_io_num; // RS485 方向控制脚 (DE/RE)
    int baud_rate;
    int port_num;   // 硬件 UART 编号 (0, 1, 2)
} bsp_rs485_config_t;

/**
 * @brief 初始化 RS485 端口
 */
void bsp_rs485_init(const bsp_rs485_config_t *config);

/**
 * @brief 发送数据 (阻塞或非阻塞由内部实现决定，建议阻塞直到发送缓冲填满)
 */
int bsp_uart_send(bsp_uart_port_t port, const uint8_t *data, size_t len);

/**
 * @brief 接收数据 (带超时)
 * @param timeout_ms 超时时间
 * @return 实际接收长度
 */
int bsp_uart_recv(bsp_uart_port_t port, uint8_t *buf, size_t max_len, uint32_t timeout_ms);

/**
 * @brief 清空输入缓冲 (每次轮询前调用)
 */
void bsp_uart_flush(bsp_uart_port_t port);

/* --- 预留给未来的网络接口 bsp_net_if.h ---
   typedef struct { ... } bsp_spi_net_config_t;
   void bsp_spi_net_init(...);
   void bsp_net_send(...);
*/