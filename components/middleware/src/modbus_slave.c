/**
 * @file modbus_slave.c
 * @brief 中间件层：Modbus RTU 从机引擎实现
 */
#include "modbus_slave.h"
#include "bsp_uart.h"
#include "register_map.h"
#include "modbus_utils.h"

static int g_slave_port = -1;
static int g_my_id = 1;

void modbus_slave_init(int uart_port, int slave_id) {
    g_slave_port = uart_port;
    g_my_id = slave_id;
}

void modbus_slave_loop(void) {
    if (g_slave_port < 0) return;
    uint8_t rx_buf[256];
    int rx_len = bsp_uart_recv(g_slave_port, rx_buf, sizeof(rx_buf), 1000);
    
    if (rx_len > 5 && rx_buf[0] == g_my_id && rx_buf[1] == 0x03) {
        uint16_t start_addr = (rx_buf[2] << 8) | rx_buf[3];
        uint16_t quantity = (rx_buf[4] << 8) | rx_buf[5];
        
        uint8_t tx_buf[256]; int tx_len = 0;
        tx_buf[tx_len++] = g_my_id;
        tx_buf[tx_len++] = 0x03;
        tx_buf[tx_len++] = quantity * 2;
        
        for (int i = 0; i < quantity; i++) {
            float val; tag_quality_t qual;
            if (reg_map_get_value(start_addr + i, &val, &qual) && qual == TAG_QUAL_GOOD) {
                uint16_t v = (uint16_t)val;
                tx_buf[tx_len++] = (v >> 8) & 0xFF;
                tx_buf[tx_len++] = v & 0xFF;
            } else {
                tx_buf[tx_len++] = 0x00;
                tx_buf[tx_len++] = 0x00;
            }
        }
        uint16_t crc = modbus_crc16(tx_buf, tx_len);
        tx_buf[tx_len++] = crc & 0xFF;
        tx_buf[tx_len++] = (crc >> 8) & 0xFF;
        
        bsp_uart_send(g_slave_port, tx_buf, tx_len);
    }
}