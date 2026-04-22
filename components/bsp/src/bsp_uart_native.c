/**
 * @file bsp_uart_native.c
 * @brief BSP层：原生 UART/RS485 面向对象封装
 * @note 实现了 bsp_serial_port_t 多态接口
 */
#include "bsp_serial_port.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include <stdlib.h>

static const char *TAG = "UART_NATIVE";

typedef struct {
    int uart_num; // 硬件 UART 端口号 (1 或 2)
} native_uart_ctx_t;

static int native_send(bsp_serial_port_t *port, const uint8_t *data, size_t len) {
    native_uart_ctx_t *ctx = (native_uart_ctx_t *)port->priv_data;
    return uart_write_bytes(ctx->uart_num, (const char *)data, len);
}

static int native_recv(bsp_serial_port_t *port, uint8_t *buf, size_t max_len, uint32_t timeout_ms) {
    native_uart_ctx_t *ctx = (native_uart_ctx_t *)port->priv_data;
    return uart_read_bytes(ctx->uart_num, buf, max_len, pdMS_TO_TICKS(timeout_ms));
}

static void native_flush(bsp_serial_port_t *port) {
    native_uart_ctx_t *ctx = (native_uart_ctx_t *)port->priv_data;
    uart_flush_input(ctx->uart_num);
}

bsp_serial_port_t* bsp_native_uart_create(int logical_port_id, int uart_num, int tx_io, int rx_io, int rts_io, int baud_rate) {
    bsp_serial_port_t *port = (bsp_serial_port_t *)calloc(1, sizeof(bsp_serial_port_t));
    native_uart_ctx_t *ctx = (native_uart_ctx_t *)calloc(1, sizeof(native_uart_ctx_t));
    
    if (!port || !ctx) {
        if (port) free(port);
        if (ctx) free(ctx);
        return NULL;
    }

    ctx->uart_num = uart_num;
    port->port_id = logical_port_id;
    port->send = native_send;
    port->recv = native_recv;
    port->flush = native_flush;
    port->priv_data = ctx;

    uart_config_t uart_conf = {
        .baud_rate = baud_rate,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    uart_driver_install(uart_num, 2048, 0, 0, NULL, 0);
    uart_param_config(uart_num, &uart_conf);
    uart_set_pin(uart_num, tx_io, rx_io, rts_io, UART_PIN_NO_CHANGE);

    // 如果配置了流控引脚，则开启 RS485 半双工模式
    if (rts_io >= 0) {
        uart_set_mode(uart_num, UART_MODE_RS485_HALF_DUPLEX);
    }
    
    // 设置 10 个字符空闲为一帧超时，加速 RTU 断帧识别
    uart_set_rx_timeout(uart_num, 10);

    ESP_LOGI(TAG, "Native UART%d (Logical %d) Initialized: TX=%d, RX=%d, DIR=%d, Baud=%d", 
             uart_num, logical_port_id, tx_io, rx_io, rts_io, baud_rate);
    return port;
}