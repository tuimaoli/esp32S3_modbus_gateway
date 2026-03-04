/**
 * @file modbus_master.c
 * @brief 中间件层：Modbus RTU 主机引擎实现，包含超时离线判定
 */
#include "modbus_master.h"
#include "bsp_uart.h"
#include "register_map.h"
#include "modbus_utils.h"
#include "esp_log.h"

static const char *TAG = "MB_MASTER";
static int g_master_port = -1;

void modbus_master_init(int uart_port) { 
    g_master_port = uart_port; 
}

void modbus_master_poll_cycle(const sensor_device_t *sensors, int count) {
    if (g_master_port < 0)
        return;

    for (int i = 0; i < count; i++) {
        const sensor_device_t *dev = &sensors[i];
        
        uint8_t tx_buf[8];
        tx_buf[0] = dev->slave_id;
        tx_buf[1] = dev->func_code;
        tx_buf[2] = (dev->start_reg >> 8) & 0xFF;
        tx_buf[3] = dev->start_reg & 0xFF;
        tx_buf[4] = (dev->reg_count >> 8) & 0xFF;
        tx_buf[5] = dev->reg_count & 0xFF;
        uint16_t crc = modbus_crc16(tx_buf, 6);
        tx_buf[6] = crc & 0xFF; tx_buf[7] = (crc >> 8) & 0xFF;

        bsp_uart_flush(g_master_port);
        bsp_uart_send(g_master_port, tx_buf, 8);

        uint8_t rx_buf[256];
        int rx_len = bsp_uart_recv(g_master_port, rx_buf, sizeof(rx_buf), 200); 

        if (rx_len > 5 && rx_buf[0] == dev->slave_id) {
            if (dev->parse_func) dev->parse_func(rx_buf, rx_len, dev->base_tag_id);
        } else {
            ESP_LOGW(TAG, "Sensor %s timeout", dev->name);
            for(int j = 0; j < dev->reg_count; j++) {
                reg_map_update_quality(dev->base_tag_id + j, TAG_QUAL_BAD_TIMEOUT);
            }
        }
    }
}