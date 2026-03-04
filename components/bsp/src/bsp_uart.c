#include "bsp_uart.h"
#include "driver/uart.h"
#include "driver/gpio.h"

#define UART_BUF_SIZE 1024

void bsp_rs485_init(const bsp_rs485_config_t *config)
{
    uart_config_t uart_conf = {
        .baud_rate = config->baud_rate,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    uart_driver_install(config->port_num, UART_BUF_SIZE * 2, 0, 0, NULL, 0);
    uart_param_config(config->port_num, &uart_conf);
    uart_set_pin(config->port_num, config->tx_io_num, config->rx_io_num, config->rts_io_num, UART_PIN_NO_CHANGE);

    uart_set_mode(config->port_num, UART_MODE_RS485_HALF_DUPLEX);
    uart_set_rx_timeout(config->port_num, 10); 
}

int bsp_uart_send(int port_num, const uint8_t *data, size_t len)
{
    // 在发送前可以手动清空输入防止回环干扰，视硬件电路而定
    // uart_flush_input(port_num); 
    return uart_write_bytes(port_num, (const char *)data, len);
}

int bsp_uart_recv(int port_num, uint8_t *buf, size_t max_len, uint32_t timeout_ms)
{
    // 这里 timeout_ms 转换为 tick
    return uart_read_bytes(port_num, buf, max_len, timeout_ms / portTICK_PERIOD_MS);
}

void bsp_uart_flush(int port_num)
{
    uart_flush_input(port_num);
}