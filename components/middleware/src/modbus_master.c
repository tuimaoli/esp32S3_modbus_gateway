/**
 * @file modbus_master.c
 * @brief 中间件层：Modbus 多模态主机引擎实现
 * @note 终极架构方案：针对 WIZnet 放弃高层 socket.h，直接操纵硬件寄存器，彻底隔绝与 LwIP 的符号冲突
 */
#include "modbus_master.h"
#include "bsp_uart.h"
#include "bsp_wifi.h"
#include "register_map.h"
#include "modbus_utils.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

// =========================================================
// 【架构演进 - 终极无冲突方案】双网络栈完美共存
// =========================================================
// 1. 直接包含 ESP-IDF 原生 LwIP 库 (供 Wi-Fi 软栈使用)
#include "lwip/sockets.h"

// 2. 抛弃 WIZnet 容易引发冲突的 "socket.h"
// 直接包含底层的寄存器与显存操作 API，对 W5100S 硬件状态机进行降维打击
#ifdef MR
#undef MR
#endif
#include "Ethernet/wizchip_conf.h"
// =========================================================

#define SOCK_MB_CLIENT_W5100S 1    

static const char __attribute__((unused)) *TAG = "MB_MASTER";
static int g_master_port = -1;
static uint16_t g_mb_tcp_tid = 0; 

// Wi-Fi Socket 连接池状态管理
static int g_wifi_mb_sock = -1;
static uint32_t g_wifi_current_ip = 0;

void modbus_master_init(int uart_port) { 
    g_master_port = uart_port; 
}

void modbus_master_poll_cycle(const sensor_device_t *sensors, int count) {
    for (int i = 0; i < count; i++) {
        const sensor_device_t *dev = &sensors[i];
        bool poll_success = false;

        /* ==========================================================
         * 链路 1：基于 RS485 的 Modbus RTU 
         * ========================================================== */
        if (dev->transport == MB_TRANSPORT_RTU && g_master_port >= 0) {
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
                poll_success = true;
            }
        } 
        /* ==========================================================
         * 链路 2：基于 W5100S 纯硬件栈的 Modbus TCP (底层寄存器重构版)
         * ========================================================== */
        else if (dev->transport == MB_TRANSPORT_TCP_W5100S) {
            uint8_t sr = getSn_SR(SOCK_MB_CLIENT_W5100S);
            uint8_t destip[4];
            getSn_DIPR(SOCK_MB_CLIENT_W5100S, destip);
            
            if (sr != SOCK_ESTABLISHED || memcmp(destip, dev->target_ip, 4) != 0) {
                // 1. 关闭状态机
                setSn_CR(SOCK_MB_CLIENT_W5100S, Sn_CR_CLOSE);
                while(getSn_CR(SOCK_MB_CLIENT_W5100S));
                setSn_IR(SOCK_MB_CLIENT_W5100S, 0xFF); // 清除残留中断标志
                
                // 2. 开启 TCP Socket (引入局部变量，完美规避宏陷阱)
                uint16_t dynamic_port = 50000 + (xTaskGetTickCount() % 10000);
                setSn_MR(SOCK_MB_CLIENT_W5100S, Sn_MR_TCP);
                setSn_PORT(SOCK_MB_CLIENT_W5100S, dynamic_port);
                setSn_CR(SOCK_MB_CLIENT_W5100S, Sn_CR_OPEN);
                while(getSn_CR(SOCK_MB_CLIENT_W5100S));
                
                // 3. 发起硬件连接指令
                setSn_DIPR(SOCK_MB_CLIENT_W5100S, (uint8_t*)dev->target_ip);
                setSn_DPORT(SOCK_MB_CLIENT_W5100S, dev->target_port);
                setSn_CR(SOCK_MB_CLIENT_W5100S, Sn_CR_CONNECT);
                while(getSn_CR(SOCK_MB_CLIENT_W5100S));
                
                int wait_ms = 500;
                while (getSn_SR(SOCK_MB_CLIENT_W5100S) != SOCK_ESTABLISHED && wait_ms > 0) {
                    vTaskDelay(pdMS_TO_TICKS(10));
                    wait_ms -= 10;
                }
            }

            if (getSn_SR(SOCK_MB_CLIENT_W5100S) == SOCK_ESTABLISHED) {
                uint8_t tx_buf[12];
                g_mb_tcp_tid++;
                tx_buf[0] = g_mb_tcp_tid >> 8;       
                tx_buf[1] = g_mb_tcp_tid & 0xFF;     
                tx_buf[2] = 0; tx_buf[3] = 0; tx_buf[4] = 0; tx_buf[5] = 6;                       
                tx_buf[6] = dev->slave_id;           
                tx_buf[7] = dev->func_code;          
                tx_buf[8] = dev->start_reg >> 8; tx_buf[9] = dev->start_reg & 0xFF;
                tx_buf[10] = dev->reg_count >> 8; tx_buf[11] = dev->reg_count & 0xFF;

                // 4. 将数据推入 W5100S 显存，触发发送状态机
                wiz_send_data(SOCK_MB_CLIENT_W5100S, tx_buf, 12);
                setSn_CR(SOCK_MB_CLIENT_W5100S, Sn_CR_SEND);
                while(getSn_CR(SOCK_MB_CLIENT_W5100S));

                uint8_t rx_buf[256];
                int rx_len = 0, timeout_ms = 500;
                while (timeout_ms > 0) {
                    rx_len = getSn_RX_RSR(SOCK_MB_CLIENT_W5100S);
                    if (rx_len > 0) break;
                    vTaskDelay(pdMS_TO_TICKS(10));
                    timeout_ms -= 10;
                }

                if (rx_len > 0) {
                    if (rx_len > sizeof(rx_buf)) rx_len = sizeof(rx_buf);
                    
                    // 5. 从显存搬运数据，并告知硬件已查收
                    wiz_recv_data(SOCK_MB_CLIENT_W5100S, rx_buf, rx_len);
                    setSn_CR(SOCK_MB_CLIENT_W5100S, Sn_CR_RECV);
                    while(getSn_CR(SOCK_MB_CLIENT_W5100S));

                    if (rx_len > 9 && rx_buf[6] == dev->slave_id) {
                        if (dev->parse_func) dev->parse_func(&rx_buf[6], rx_len - 6, dev->base_tag_id);
                        poll_success = true;
                    }
                }
            }
        }
        /* ==========================================================
         * 链路 3：基于 ESP32 Wi-Fi (LwIP 软栈) 的 Modbus TCP
         * ========================================================== */
        else if (dev->transport == MB_TRANSPORT_TCP_WIFI) {
            if (bsp_wifi_is_connected()) {
                uint32_t target_ip_u32 = *((uint32_t*)dev->target_ip);
                
                if (g_wifi_mb_sock >= 0 && g_wifi_current_ip != target_ip_u32) {
                    lwip_close(g_wifi_mb_sock); 
                    g_wifi_mb_sock = -1;
                }

                if (g_wifi_mb_sock < 0) {
                    g_wifi_mb_sock = lwip_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
                    if (g_wifi_mb_sock >= 0) {
                        struct timeval tv = { .tv_sec = 0, .tv_usec = 500000 };
                        lwip_setsockopt(g_wifi_mb_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
                        lwip_setsockopt(g_wifi_mb_sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

                        struct sockaddr_in dest_addr = {0};
                        dest_addr.sin_family = AF_INET;
                        dest_addr.sin_port = htons(dev->target_port);
                        memcpy(&dest_addr.sin_addr.s_addr, dev->target_ip, 4);

                        if (lwip_connect(g_wifi_mb_sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr)) == 0) {
                            g_wifi_current_ip = target_ip_u32;
                        } else {
                            lwip_close(g_wifi_mb_sock);
                            g_wifi_mb_sock = -1;
                        }
                    }
                }

                if (g_wifi_mb_sock >= 0) {
                    uint8_t tx_buf[12];
                    g_mb_tcp_tid++;
                    tx_buf[0] = g_mb_tcp_tid >> 8; tx_buf[1] = g_mb_tcp_tid & 0xFF;     
                    tx_buf[2] = 0; tx_buf[3] = 0; tx_buf[4] = 0; tx_buf[5] = 6;                       
                    tx_buf[6] = dev->slave_id; tx_buf[7] = dev->func_code;          
                    tx_buf[8] = dev->start_reg >> 8; tx_buf[9] = dev->start_reg & 0xFF;
                    tx_buf[10] = dev->reg_count >> 8; tx_buf[11] = dev->reg_count & 0xFF;

                    if (lwip_send(g_wifi_mb_sock, tx_buf, 12, 0) == 12) {
                        uint8_t rx_buf[256];
                        int rx_len = lwip_recv(g_wifi_mb_sock, rx_buf, sizeof(rx_buf), 0);
                        if (rx_len > 9 && rx_buf[6] == dev->slave_id) {
                            if (dev->parse_func) dev->parse_func(&rx_buf[6], rx_len - 6, dev->base_tag_id);
                            poll_success = true;
                        } else if (rx_len <= 0) {
                            lwip_close(g_wifi_mb_sock);
                            g_wifi_mb_sock = -1;
                        }
                    } else {
                        lwip_close(g_wifi_mb_sock);
                        g_wifi_mb_sock = -1;
                    }
                }
            }
        }

        /* ==========================================================
         * 统一防呆处理：失败打上 Bad 质量戳
         * ========================================================== */
        if (!poll_success) {
            reg_map_update_quality(dev->status_tag_id, TAG_QUAL_BAD_TIMEOUT);
        } else {
            reg_map_update_value(dev->status_tag_id, 1.0f);
        }
        
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}