/**
 * @file app_tcp_server.c
 * @brief 应用层：基于 W5100S 的硬件 TCP Server
 */
#include "app_tcp_server.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "register_map.h"

#ifdef MR
#undef MR
#endif
#include "Ethernet/socket.h" 

#define PORT 5020
#define SOCK_TCPS 0 

static void tcp_server_task(void *pvParameters) {
    uint8_t rx_buffer[128];
    while (1) {
        switch (getSn_SR(SOCK_TCPS)) {
        case SOCK_INIT: 
            listen(SOCK_TCPS);
            break;
        case SOCK_ESTABLISHED:
            if (getSn_IR(SOCK_TCPS) & Sn_IR_CON)
                setSn_IR(SOCK_TCPS, Sn_IR_CON); 
            int len = getSn_RX_RSR(SOCK_TCPS);
            if (len > 0) {
                recv(SOCK_TCPS, rx_buffer, len);
                if (rx_buffer[0] == 0x03) {
                    uint16_t tag_id = (rx_buffer[1] << 8) | rx_buffer[2];
                    float val = 0; tag_quality_t qual;
                    if (reg_map_get_value(tag_id, &val, &qual)) {
                        if (qual == TAG_QUAL_GOOD) {
                            uint16_t int_val = (uint16_t)val;
                            uint8_t tx_buf[2] = { (int_val >> 8) & 0xFF, int_val & 0xFF };
                            send(SOCK_TCPS, tx_buf, 2);
                        } else 
                            send(SOCK_TCPS, (uint8_t*)"OFF", 3);
                    } else
                        send(SOCK_TCPS, (uint8_t*)"INV", 3);
                } else if (rx_buffer[0] == 0x06) {
                    uint16_t tag_id = (rx_buffer[1] << 8) | rx_buffer[2];
                    uint16_t set_val = (rx_buffer[3] << 8) | rx_buffer[4];
                    reg_map_update_value(tag_id, (float)set_val);
                    send(SOCK_TCPS, (uint8_t*)"OK", 2);
                }
            }
            break;
        case SOCK_CLOSE_WAIT:
            disconnect(SOCK_TCPS); 
            break;
        case SOCK_CLOSED:
            socket(SOCK_TCPS, Sn_MR_TCP, PORT, 0x00);
            break;
        default: 
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(10)); 
    }
}

void app_tcp_server_start(void) {
    xTaskCreate(tcp_server_task, "tcp_server", 4096, NULL, 5, NULL);
}