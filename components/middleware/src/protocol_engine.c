/**
 * @file protocol_engine.c
 * @brief 中间件层：泛化协议引擎 (SDH 软件定义硬件核心)
 * @note 彻底解决粘包/半包问题，支持切片规则映射与 MQTT 异步入队
 */
#include "protocol_engine.h"
#include "bsp_uart.h"
#include "register_map.h"
#include "modbus_utils.h"
#include "app_mqtt.h"      // 引入 MQTT 发送队列
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

#ifdef MR
#undef MR
#endif
#include "Ethernet/wizchip_conf.h"

static const char *TAG = "PROTO_ENG";
static int g_master_port = -1;

#define SOCK_W5100S_CLIENT 1    // W5100S 协议引擎专用 Socket

// 每个链路的物理滑动接收窗 (应对粘包半包)
static uint8_t g_rs485_rx_ring[1024];
static int g_rs485_rx_len = 0;

void protocol_engine_init(int uart_port) { 
    g_master_port = uart_port; 
    g_rs485_rx_len = 0;
}

/* ============================================================
 * 内部引擎：切片映射与入库 (数据萃取器)
 * ============================================================ */
static void execute_slice_mapping(const sensor_device_t *dev, const uint8_t *frame, int frame_len) {
    for (int i = 0; i < dev->rule_count; i++) {
        const modbus_mapping_rule_t *rule = &dev->rules[i];
        
        // 越界防呆
        if (rule->byte_offset + 2 > frame_len) continue;

        const uint8_t *p = &frame[rule->byte_offset];
        float final_val = 0.0f;

        // 多态数据类型萃取 (大端/小端/浮点转换)
        switch (rule->type) {
            case MB_TYPE_UINT16_AB:
                final_val = (float)((p[0] << 8) | p[1]); break;
            case MB_TYPE_UINT16_BA:
                final_val = (float)((p[1] << 8) | p[0]); break;
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
            default:
                final_val = (float)p[0]; // 回退为单字节
        }

        final_val *= rule->scale;
        
        // 1. 更新本地 RTDB
        reg_map_update_value(rule->target_tag_id, final_val);
        
        // 2. ⚡ 极速非阻塞：将切片好的数据压入 MQTT 发送队列
        app_mqtt_enqueue_data(dev->name, rule->name, final_val);
    }
}

/* ============================================================
 * 内部引擎：非标协议滑动窗口分帧器
 * ============================================================ */
static bool process_custom_sliding_window(const sensor_device_t *dev, uint8_t *buffer, int *buf_len) {
    bool parsed_success = false;
    int i = 0;
    
    // 滑动寻找帧头
    while (i <= *buf_len - dev->custom.header_len) {
        if (memcmp(&buffer[i], dev->custom.header, dev->custom.header_len) == 0) {
            int frame_start = i;
            int frame_len = 0;
            bool frame_found = false;

            // 根据模式寻找帧尾或截取定长
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

            // 成功截取出一帧
            if (frame_found) {
                execute_slice_mapping(dev, &buffer[frame_start], frame_len);
                parsed_success = true;
                
                // 将处理完的数据移出缓冲区 (RingBuffer Pop 操作)
                int remaining = *buf_len - (frame_start + frame_len);
                if (remaining > 0) memmove(buffer, &buffer[frame_start + frame_len], remaining);
                *buf_len = remaining;
                i = 0; // 重新从头扫描剩下的碎片
                continue;
            }
        }
        i++;
    }
    
    // 防爆池保护：如果一直找不到帧头导致池子快满了，强制清空一半废弃物
    if (*buf_len > (sizeof(g_rs485_rx_ring) - 128)) {
        memmove(buffer, &buffer[*buf_len / 2], *buf_len / 2);
        *buf_len /= 2;
    }
    return parsed_success;
}

/* ============================================================
 * 核心对外接口：主循环调度
 * ============================================================ */
void protocol_engine_poll_cycle(const sensor_device_t *sensors, int count) {
    for (int i = 0; i < count; i++) {
        const sensor_device_t *dev = &sensors[i];
        bool link_alive = false;

        /* ==========================================================
         * 链路处理 A：RS485 物理总线 (多态协议分发)
         * ========================================================== */
        if (dev->transport == 0 && g_master_port >= 0) {
            
            // 如果是一问一答型 (Modbus 或 Custom Poll)，下发 TX
            if (dev->protocol == PROTO_MODBUS_RTU) {
                uint8_t tx_buf[8];
                tx_buf[0] = dev->slave_id; tx_buf[1] = dev->func_code;
                tx_buf[2] = dev->start_reg >> 8; tx_buf[3] = dev->start_reg & 0xFF;
                tx_buf[4] = dev->reg_count >> 8; tx_buf[5] = dev->reg_count & 0xFF;
                uint16_t crc = modbus_crc16(tx_buf, 6);
                tx_buf[6] = crc & 0xFF; tx_buf[7] = crc >> 8;
                bsp_uart_flush(g_master_port);
                bsp_uart_send(g_master_port, tx_buf, 8);
            } else if (dev->protocol == PROTO_CUSTOM_POLL && dev->custom.tx_len > 0) {
                bsp_uart_flush(g_master_port);
                bsp_uart_send(g_master_port, dev->custom.tx_payload, dev->custom.tx_len);
            }

            // 无论哪种协议 (包括纯监听的 REPORT 模式)，都去读串口追加到缓冲池
            uint8_t temp_rx[256];
            int rx_len = bsp_uart_recv(g_master_port, temp_rx, sizeof(temp_rx), dev->timeout_ms > 0 ? dev->timeout_ms : 50);
            
            if (rx_len > 0 && (g_rs485_rx_len + rx_len < sizeof(g_rs485_rx_ring))) {
                memcpy(&g_rs485_rx_ring[g_rs485_rx_len], temp_rx, rx_len);
                g_rs485_rx_len += rx_len;
            }

            // 数据解析分发
            if (dev->protocol == PROTO_MODBUS_RTU) {
                // Modbus 固定断帧验证
                if (g_rs485_rx_len >= 5 && g_rs485_rx_ring[0] == dev->slave_id) {
                    execute_slice_mapping(dev, &g_rs485_rx_ring[3], g_rs485_rx_len - 5);
                    g_rs485_rx_len = 0; // Modbus 一问一答结束，清空池子
                    link_alive = true;
                }
            } else if (dev->protocol == PROTO_CUSTOM_POLL || dev->protocol == PROTO_CUSTOM_REPORT) {
                // 非标协议滑动窗口分帧
                if (process_custom_sliding_window(dev, g_rs485_rx_ring, &g_rs485_rx_len)) {
                    link_alive = true;
                }
            }
        } 
        /* ==========================================================
         * 链路处理 B：W5100S 硬件 TCP (暂演示标准 Modbus TCP)
         * ========================================================== */
        else if (dev->transport == 1 && dev->protocol == PROTO_MODBUS_TCP) {
            uint8_t sr = getSn_SR(SOCK_W5100S_CLIENT);
            if (sr != SOCK_ESTABLISHED) {
                setSn_CR(SOCK_W5100S_CLIENT, Sn_CR_CLOSE); while(getSn_CR(SOCK_W5100S_CLIENT));
                setSn_MR(SOCK_W5100S_CLIENT, Sn_MR_TCP);
                setSn_PORT(SOCK_W5100S_CLIENT, 50000 + (xTaskGetTickCount() % 10000));
                setSn_CR(SOCK_W5100S_CLIENT, Sn_CR_OPEN); while(getSn_CR(SOCK_W5100S_CLIENT));
                setSn_DIPR(SOCK_W5100S_CLIENT, (uint8_t*)dev->target_ip);
                setSn_DPORT(SOCK_W5100S_CLIENT, dev->target_port);
                setSn_CR(SOCK_W5100S_CLIENT, Sn_CR_CONNECT); while(getSn_CR(SOCK_W5100S_CLIENT));
                vTaskDelay(pdMS_TO_TICKS(100));
            }

            if (getSn_SR(SOCK_W5100S_CLIENT) == SOCK_ESTABLISHED) {
                uint8_t tx_buf[12] = {0,0,0,0,0,6, dev->slave_id, dev->func_code, 
                                      dev->start_reg>>8, dev->start_reg&0xFF, dev->reg_count>>8, dev->reg_count&0xFF};
                wiz_send_data(SOCK_W5100S_CLIENT, tx_buf, 12);
                setSn_CR(SOCK_W5100S_CLIENT, Sn_CR_SEND); while(getSn_CR(SOCK_W5100S_CLIENT));

                int wait = dev->timeout_ms;
                while(getSn_RX_RSR(SOCK_W5100S_CLIENT) == 0 && wait > 0) {
                    vTaskDelay(pdMS_TO_TICKS(10)); wait -= 10;
                }

                int rx_len = getSn_RX_RSR(SOCK_W5100S_CLIENT);
                if (rx_len > 9) {
                    uint8_t rx_buf[256];
                    if (rx_len > 256) rx_len = 256;
                    wiz_recv_data(SOCK_W5100S_CLIENT, rx_buf, rx_len);
                    setSn_CR(SOCK_W5100S_CLIENT, Sn_CR_RECV); while(getSn_CR(SOCK_W5100S_CLIENT));
                    
                    if (rx_buf[6] == dev->slave_id) {
                        execute_slice_mapping(dev, &rx_buf[9], rx_len - 9);
                        link_alive = true;
                    }
                }
            }
        }

        // 统一链路健康度管理
        reg_map_update_value(dev->status_tag_id, link_alive ? 1.0f : 0.0f);
        
        // 基于轮询间隔进行高级调度挂起 (防止占死 CPU)
        if (dev->protocol != PROTO_CUSTOM_REPORT && dev->poll_interval_ms > 0) {
            vTaskDelay(pdMS_TO_TICKS(dev->poll_interval_ms));
        }
    }
}