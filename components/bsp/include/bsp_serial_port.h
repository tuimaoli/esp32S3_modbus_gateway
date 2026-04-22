/**
 * @file bsp_serial_port.h
 * @brief BSP层：通用串口面向对象 (OOP) 抽象接口
 * @note 屏蔽原生 UART 与 I2C/SPI 拓展 UART (如 SC16IS750) 的差异。
 * Middleware 层的所有协议引擎将只依赖此接口，彻底与物理层解耦。
 */
#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// 前向声明句柄类型
typedef struct bsp_serial_port_s bsp_serial_port_t;

/**
 * @brief 通用串口操作函数指针表 (Virtual Table)
 */
struct bsp_serial_port_s {
    int port_id;  ///< 逻辑端口号 (对应 JSON 组态中的 ID)
    
    /**
     * @brief 发送数据块
     * @return 实际发送的字节数，<0 代表错误
     */
    int (*send)(bsp_serial_port_t *port, const uint8_t *data, size_t len);
    
    /**
     * @brief 接收数据块 (阻塞等待直到超时)
     * @return 实际接收的字节数
     */
    int (*recv)(bsp_serial_port_t *port, uint8_t *buf, size_t max_len, uint32_t timeout_ms);
    
    /**
     * @brief 清空硬件接收与发送缓存
     */
    void (*flush)(bsp_serial_port_t *port);
    
    /**
     * @brief 私有上下文指针 (指向具体的 bsp_uart_native 或 bsp_sc16is750 对象)
     */
    void *priv_data;
};

#ifdef __cplusplus
}
#endif