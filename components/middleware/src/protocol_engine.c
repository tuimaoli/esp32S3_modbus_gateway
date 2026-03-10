/**
 * @file protocol_engine.c
 * @brief 中间件层：泛化协议引擎 (SDH 软件定义硬件核心)
 * @note 已应用严格的企业级代码规范，展开所有条件/循环层级，修复宏优先级陷阱
 */
#include "protocol_engine.h"
#include "bsp_uart.h"
#include "register_map.h"
#include "modbus_utils.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

#ifdef MR
#undef MR
#endif
#include "Ethernet/wizchip_conf.h"

extern void app_mqtt_enqueue_data(const char *sensor_name, const char *metric_name, float value);

static const char __attribute__((unused)) *TAG = "PROTO_ENG";
static int g_master_port = -1;

#define SOCK_W5100S_CLIENT 1    

static uint8_t g_rs485_rx_ring[1024];
static int g_rs485_rx_len = 0;

void protocol_engine_init(int uart_port) { 
    g_master_port = uart_port; 
    g_rs485_rx_len = 0;
    ESP_LOGI(TAG, "Protocol Engine Initialized on UART%d", uart_port);
}

static void execute_slice_mapping(const sensor_device_t *dev, const uint8_t *frame, int frame_len) {
    for (int i = 0; i < dev->rule_count; i++) {
        const modbus_mapping_rule_t *rule = &dev->rules[i];
        
        if (rule->byte_offset + 2 > frame_len) {
            continue;
        }

        const uint8_t *p = &frame[rule->byte_offset];
        float final_val = 0.0f;

        switch (rule->type) {
            case MB_TYPE_UINT16_AB:
                final_val = (float)((p[0] << 8) | p[1]); 
                break;
            case MB_TYPE_UINT16_BA:
                final_val = (float)((p[1] << 8) | p[0]); 
                break;
            case MB_TYPE_FLOAT32_ABCD:
                if (rule->byte_offset + 4 <= frame_len) {
                    uint32_t temp = (p[0]<<24) | (p[1]<<16) | (p[2]<<8) | p[3];
                    final_val = *((float*)&temp);
                }
                break;
            case MB_TYPE_FLOAT32_CDAB:
                if (rule->byte_offset + 4 <= frame_len) {
                    uint32_t temp = (p[2]<<24) | (p[3]<<16) | (p[0]<<8) | p[1];
                    final_val = *((float*)&temp);
                }
                break;
            case MB_TYPE_FLOAT32_DCBA:
                if (rule->byte_offset + 4 <= frame_len) {
                    uint32_t temp = (p[3]<<24) | (p[2]<<16) | (p[1]<<8) | p[0];
                    final_val = *((float*)&temp);
                }
                break;
            case MB_TYPE_FLOAT32_BADC:
                if (rule->byte_offset + 4 <= frame_len) {
                    uint32_t temp = (p[1]<<24) | (p[0]<<16) | (p[3]<<8) | p[2];
                    final_val = *((float*)&temp);
                }
                break;
            default:
                final_val = (float)p[0]; 
                break;
        }

        final_val *= rule->scale;
        
        reg_map_update_value(rule->target_tag_id, final_val);
        app_mqtt_enqueue_data(dev->name, rule->name, final_val);
    }
}

static bool process_custom_sliding_window(const sensor_device_t *dev, uint8_t *buffer, int *buf_len) {
    bool parsed_success = false;
    int i = 0;
    
    while (i <= *buf_len - dev->custom.header_len) {
        if (memcmp(&buffer[i], dev->custom.header, dev->custom.header_len) == 0) {
            int frame_start = i;
            int frame_len = 0;
            bool frame_found = false;

            if (dev->custom.frame_mode == MODE_HEAD_TAIL) {
                for (int j = frame_start + dev->custom.header_len; j <= *buf_len - dev->custom.footer_len; j++) {
                    if (memcmp(&buffer[j], dev->custom.footer, dev->custom.footer_len) == 0) {
                        frame_len = (j + dev->custom.footer_len) - frame_start;
                        frame_found = true;
                        break;
                    }
                }
            } else if (dev->custom.frame_mode == MODE_HEAD_FIXED) {
                if (*buf_len - frame_start >= dev->custom.fixed_len) {
                    frame_len = dev->custom.fixed_len;
                    frame_found = true;
                }
            }

            if (frame_found) {
                execute_slice_mapping(dev, &buffer[frame_start], frame_len);
                parsed_success = true;
                
                int remaining = *buf_len - (frame_start + frame_len);
                if (remaining > 0) {
                    memmove(buffer, &buffer[frame_start + frame_len], remaining);
                }
                *buf_len = remaining;
                i = 0; 
                continue;
            }
        }
        i++;
    }
    
    if (*buf_len > (sizeof(g_rs485_rx_ring) - 128)) {
        memmove(buffer, &buffer[*buf_len / 2], *buf_len / 2);
        *buf_len /= 2;
    }
    
    return parsed_success;
}

void protocol_engine_poll_cycle(const sensor_device_t *sensors, int count) {
    for (int i = 0; i < count; i++) {
        const sensor_device_t *dev = &sensors[i];
        bool link_alive = false;

        if (dev->transport == 0 && g_master_port >= 0) {
            
            if (dev->protocol == PROTO_MODBUS_RTU) {
                uint8_t tx_buf[8];
                tx_buf[0] = dev->slave_id; 
                tx_buf[1] = dev->func_code;
                tx_buf[2] = dev->start_reg >> 8; 
                tx_buf[3] = dev->start_reg & 0xFF;
                tx_buf[4] = dev->reg_count >> 8; 
                tx_buf[5] = dev->reg_count & 0xFF;
                
                uint16_t crc = modbus_crc16(tx_buf, 6);
                tx_buf[6] = crc & 0xFF; 
                tx_buf[7] = crc >> 8;
                
                bsp_uart_flush(g_master_port);
                bsp_uart_send(g_master_port, tx_buf, 8);
            } else if (dev->protocol == PROTO_CUSTOM_POLL && dev->custom.tx_len > 0) {
                bsp_uart_flush(g_master_port);
                bsp_uart_send(g_master_port, dev->custom.tx_payload, dev->custom.tx_len);
            }

            uint8_t temp_rx[256];
            int wait_time = dev->timeout_ms > 0 ? dev->timeout_ms : 50;
            int rx_len = bsp_uart_recv(g_master_port, temp_rx, sizeof(temp_rx), wait_time);
            
            if (rx_len > 0 && (g_rs485_rx_len + rx_len < sizeof(g_rs485_rx_ring))) {
                memcpy(&g_rs485_rx_ring[g_rs485_rx_len], temp_rx, rx_len);
                g_rs485_rx_len += rx_len;
            }

            if (dev->protocol == PROTO_MODBUS_RTU) {
                if (g_rs485_rx_len >= 5 && g_rs485_rx_ring[0] == dev->slave_id) {
                    execute_slice_mapping(dev, &g_rs485_rx_ring[3], g_rs485_rx_len - 5);
                    g_rs485_rx_len = 0; 
                    link_alive = true;
                }
            } else if (dev->protocol == PROTO_CUSTOM_POLL || dev->protocol == PROTO_CUSTOM_REPORT) {
                if (process_custom_sliding_window(dev, g_rs485_rx_ring, &g_rs485_rx_len)) {
                    link_alive = true;
                }
            }
        } 
        else if (dev->transport == 1 && dev->protocol == PROTO_MODBUS_TCP) {
            uint8_t sr = getSn_SR(SOCK_W5100S_CLIENT);
            if (sr != SOCK_ESTABLISHED) {
                setSn_CR(SOCK_W5100S_CLIENT, Sn_CR_CLOSE); 
                while (getSn_CR(SOCK_W5100S_CLIENT)) {
                    // 等待关闭状态机执行完毕
                }
                
                uint16_t dynamic_port = 50000 + (xTaskGetTickCount() % 10000);
                
                setSn_MR(SOCK_W5100S_CLIENT, Sn_MR_TCP);
                setSn_PORT(SOCK_W5100S_CLIENT, dynamic_port);
                setSn_CR(SOCK_W5100S_CLIENT, Sn_CR_OPEN); 
                while (getSn_CR(SOCK_W5100S_CLIENT)) {
                    // 等待打开状态机执行完毕
                }
                
                setSn_DIPR(SOCK_W5100S_CLIENT, (uint8_t*)dev->target_ip);
                setSn_DPORT(SOCK_W5100S_CLIENT, dev->target_port);
                setSn_CR(SOCK_W5100S_CLIENT, Sn_CR_CONNECT); 
                while (getSn_CR(SOCK_W5100S_CLIENT)) {
                    // 等待连接指令发出
                }
                
                vTaskDelay(pdMS_TO_TICKS(100));
            }

            if (getSn_SR(SOCK_W5100S_CLIENT) == SOCK_ESTABLISHED) {
                uint8_t tx_buf[12] = {0, 0, 0, 0, 0, 6, dev->slave_id, dev->func_code, 
                                      dev->start_reg >> 8, dev->start_reg & 0xFF, 
                                      dev->reg_count >> 8, dev->reg_count & 0xFF};
                wiz_send_data(SOCK_W5100S_CLIENT, tx_buf, 12);
                setSn_CR(SOCK_W5100S_CLIENT, Sn_CR_SEND); 
                while (getSn_CR(SOCK_W5100S_CLIENT)) {
                    // 等待数据发送完毕
                }

                int wait = dev->timeout_ms;
                while (getSn_RX_RSR(SOCK_W5100S_CLIENT) == 0 && wait > 0) {
                    vTaskDelay(pdMS_TO_TICKS(10)); 
                    wait -= 10;
                }

                int rx_len = getSn_RX_RSR(SOCK_W5100S_CLIENT);
                if (rx_len > 9) {
                    uint8_t rx_buf[256];
                    if (rx_len > 256) {
                        rx_len = 256;
                    }
                    wiz_recv_data(SOCK_W5100S_CLIENT, rx_buf, rx_len);
                    setSn_CR(SOCK_W5100S_CLIENT, Sn_CR_RECV); 
                    while (getSn_CR(SOCK_W5100S_CLIENT)) {
                        // 等待数据接收标志清除
                    }
                    
                    if (rx_buf[6] == dev->slave_id) {
                        execute_slice_mapping(dev, &rx_buf[9], rx_len - 9);
                        link_alive = true;
                    }
                }
            }
        }

        reg_map_update_value(dev->status_tag_id, link_alive ? 1.0f : 0.0f);
        
        if (dev->protocol != PROTO_CUSTOM_REPORT && dev->poll_interval_ms > 0) {
            vTaskDelay(pdMS_TO_TICKS(dev->poll_interval_ms));
        }
    }
}