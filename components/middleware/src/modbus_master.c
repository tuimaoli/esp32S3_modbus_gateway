/**
 * @file modbus_master.c
 * @brief 中间件层：Modbus 多模态主机引擎实现
 * @note 精妙处理了 WIZnet 硬件 Socket 与 LwIP 软件 Socket 的宏隔离
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
// 【架构魔术】解决双网络栈宏冲突
// 1. 先包含 ESP-IDF 原生 LwIP 库
#include "lwip/sockets.h"

// 2. 剥除 LwIP 的通用宏定义，强迫后续 Wi-Fi 代码使用 lwip_ 前缀
#undef socket
#undef connect
#undef send
#undef recv
#undef close

// 3. 包含 WIZnet 硬件库 (它将接管通用的 socket, connect 等宏)
#ifdef MR
#undef MR
#endif
#include "Ethernet/socket.h"
// =========================================================

#define SOCK_MB_CLIENT_W5100S 1    

static const char *TAG = "MB_MASTER";
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
         * 链路 2：基于 W5100S 纯硬件栈的 Modbus TCP
         * ========================================================== */
        else if (dev->transport == MB_TRANSPORT_TCP_W5100S) {
            uint8_t sr = getSn_SR(SOCK_MB_CLIENT_W5100S);
            uint8_t destip[4];
            getSn_DIPR(SOCK_MB_CLIENT_W5100S, destip);
            
            if (sr != SOCK_ESTABLISHED || memcmp(destip, dev->target_ip, 4) != 0) {
                close(SOCK_MB_CLIENT_W5100S); // WIZnet API
                socket(SOCK_MB_CLIENT_W5100S, Sn_MR_TCP, 50000 + (xTaskGetTickCount() % 10000), 0);
                connect(SOCK_MB_CLIENT_W5100S, (uint8_t*)dev->target_ip, dev->target_port);
                
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

                send(SOCK_MB_CLIENT_W5100S, tx_buf, 12); // WIZnet API

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
                    recv(SOCK_MB_CLIENT_W5100S, rx_buf, rx_len); // WIZnet API
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
            // 只有当 Wi-Fi 获取到 IP 后，才发起底层 Socket 连接
            if (bsp_wifi_is_connected()) {
                uint32_t target_ip_u32 = *((uint32_t*)dev->target_ip);
                
                // IP 改变时重置连接池
                if (g_wifi_mb_sock >= 0 && g_wifi_current_ip != target_ip_u32) {
                    lwip_close(g_wifi_mb_sock); // 必须使用 lwip_ 前缀
                    g_wifi_mb_sock = -1;
                }

                // 按需建连
                if (g_wifi_mb_sock < 0) {
                    g_wifi_mb_sock = lwip_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
                    if (g_wifi_mb_sock >= 0) {
                        // 设置 500ms 超时，防止阻塞轮询任务
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

                // 建连成功，发送并接收
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
                            // 远端断开或超时
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
         * 统一防呆处理：不管是什么链路，失败了统统打上 Bad 质量戳
         * ========================================================== */
        if (!poll_success) {
            reg_map_update_quality(dev->status_tag_id, TAG_QUAL_BAD_TIMEOUT);
        } else {
            reg_map_update_value(dev->status_tag_id, 1.0f);
        }
        
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}