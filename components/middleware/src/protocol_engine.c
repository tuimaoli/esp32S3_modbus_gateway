/**
 * @file protocol_engine.c
 * @brief 中间件层：泛化协议引擎 (SDH 软件定义硬件核心 V4.2 终极版)
 * @note 1. 全面拥抱 OOP 串口轮询机制，支持千手观音般的多线程并发。
 * 2. 针对 W5100S 放弃高层 socket.h，直接操纵硬件寄存器，彻底隔绝与 LwIP 的符号冲突。
 */
#include "protocol_engine.h"
#include "register_map.h"
#include "utils.h"       
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <string.h>

// =========================================================
// 【双网络栈完美共存方案】
// =========================================================
// 1. Wi-Fi 软栈使用原生 LwIP
#include "lwip/sockets.h"
#include "lwip/netdb.h"

// 2. 硬件以太网 W5100S 直接操纵底层寄存器，不用它的 Socket 宏
#ifdef MR
#undef MR
#endif
#include "Ethernet/wizchip_conf.h"
// =========================================================

static const char *TAG = "PROTO_ENG";

#define SOCK_W5100S_CLIENT 1    // W5100S 协议引擎专用 Socket 硬件通道

// 全局回调函数，用于将数据抛给 MQTT
static protocol_data_cb_t g_data_cb = NULL;

// 反向控制发射队列 (TX Queue)，允许跨线程安全下发写指令
typedef struct {
    uint8_t slave_id;
    uint16_t reg_addr;
    float value;
} tx_req_t;

static QueueHandle_t g_tx_queue = NULL;

// ============================================================
// 工具集：钩子注册与 TX 队列
// ============================================================
void protocol_engine_register_data_cb(protocol_data_cb_t cb) {
    g_data_cb = cb;
}

bool protocol_engine_push_tx_queue(uint8_t slave_id, uint16_t reg_addr, float value) {
    if (g_tx_queue == NULL) {
        g_tx_queue = xQueueCreate(20, sizeof(tx_req_t));
    }
    tx_req_t req = { .slave_id = slave_id, .reg_addr = reg_addr, .value = value };
    // 非阻塞压入，如果满了则丢弃并报警
    if (xQueueSend(g_tx_queue, &req, 0) == pdTRUE) {
        return true;
    }
    ESP_LOGE(TAG, "TX Queue Full! Drop Write Req (Slave: %d, Reg: %d)", slave_id, reg_addr);
    return false;
}

// ============================================================
// 核心数据清洗层：字典切片与 RTDB 注入
// ============================================================
static void execute_slice_mapping(sensor_device_t *dev, const uint8_t *data_payload, int payload_len) {
    for (int i = 0; i < dev->rule_count; i++) {
        modbus_mapping_rule_t *rule = &dev->rules[i];
        
        // 边界保护防越界崩溃
        int req_bytes = (rule->type >= MB_TYPE_UINT32_ABCD) ? 4 : 2;
        if (rule->offset + req_bytes > payload_len) continue;

        const uint8_t *p = &data_payload[rule->offset];
        float final_val = 0.0f;
        
        // C11 多态字节序极速解析树
        switch (rule->type) {
            case MB_TYPE_UINT16_AB:
                final_val = (float)((p[0] << 8) | p[1]); break;
            case MB_TYPE_UINT16_BA:
                final_val = (float)((p[1] << 8) | p[0]); break;
            case MB_TYPE_INT16_AB:
                final_val = (float)((int16_t)((p[0] << 8) | p[1])); break;
            case MB_TYPE_INT16_BA:
                final_val = (float)((int16_t)((p[1] << 8) | p[0])); break;
            case MB_TYPE_UINT32_ABCD:
                final_val = (float)((p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]); break;
            case MB_TYPE_UINT32_CDAB:
                final_val = (float)((p[2] << 24) | (p[3] << 16) | (p[0] << 8) | p[1]); break;
            case MB_TYPE_FLOAT32_ABCD: {
                uint32_t temp = (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
                memcpy(&final_val, &temp, 4); break;
            }
            case MB_TYPE_FLOAT32_CDAB: { // 西门子 PLC 常用浮点格式
                uint32_t temp = (p[2] << 24) | (p[3] << 16) | (p[0] << 8) | p[1];
                memcpy(&final_val, &temp, 4); break;
            }
            default: continue;
        }

        // 线性变换标定
        final_val = final_val * rule->scale;
        
        // 1. 注入中间件大脑 RTDB (假设外部存在 update 接口，如果不存在则调用底层 write 忽略权限)
        extern void reg_map_update_value(uint16_t tag_id, float value); 
        reg_map_update_value(rule->target_tag_id, final_val);
        
        // 2. 触发回调推至云端
        if (g_data_cb) {
            g_data_cb(dev->name, rule->name, final_val);
        }
    }
}

// ============================================================
// 引擎分支 1：面向对象的多串口轮询引擎 (支持并发调用)
// ============================================================
void protocol_engine_poll_serial_cycle(bsp_serial_port_t *port, sensor_device_t *sensors, int count) {
    if (!port || !sensors || count <= 0) return;

    if (g_tx_queue == NULL) {
        g_tx_queue = xQueueCreate(20, sizeof(tx_req_t));
    }

    uint8_t tx_buf[256];
    uint8_t rx_buf[256];

    for (int i = 0; i < count; i++) {
        sensor_device_t *dev = &sensors[i];

        // --- A. 优先处理高优先级的反向写控制指令 (TX Queue) ---
        tx_req_t req;
        if (xQueueReceive(g_tx_queue, &req, 0) == pdTRUE) {
            // 目前采用广播探测，不管在哪个串口，只要 Slave ID 匹配就下发
            if (req.slave_id == dev->slave_id) {
                tx_buf[0] = req.slave_id;
                tx_buf[1] = 0x06; // 写单个寄存器
                tx_buf[2] = req.reg_addr >> 8;
                tx_buf[3] = req.reg_addr & 0xFF;
                uint16_t int_val = (uint16_t)req.value;
                tx_buf[4] = int_val >> 8;
                tx_buf[5] = int_val & 0xFF;
                uint16_t crc = utils_crc16_modbus(tx_buf, 6);
                tx_buf[6] = crc & 0xFF;
                tx_buf[7] = (crc >> 8) & 0xFF;

                port->flush(port);
                port->send(port, tx_buf, 8);
                port->recv(port, rx_buf, sizeof(rx_buf), dev->timeout_ms);
                vTaskDelay(pdMS_TO_TICKS(10)); // 防止总线拥堵，让出间隙
            } else {
                // 如果不是本轮询组的设备，重新塞回队列尾部
                xQueueSendToBack(g_tx_queue, &req, 0); 
            }
        }

        // --- B. 执行正常的周期性读轮询 ---
        if (dev->protocol == PROTO_MODBUS_RTU) {
            tx_buf[0] = dev->slave_id;
            tx_buf[1] = dev->func_code;
            tx_buf[2] = dev->start_reg >> 8;
            tx_buf[3] = dev->start_reg & 0xFF;
            tx_buf[4] = dev->reg_count >> 8;
            tx_buf[5] = dev->reg_count & 0xFF;
            uint16_t crc = utils_crc16_modbus(tx_buf, 6);
            tx_buf[6] = crc & 0xFF;
            tx_buf[7] = (crc >> 8) & 0xFF;

            port->flush(port);
            port->send(port, tx_buf, 8);

            // 阻塞等待响应
            int expected_len = 5 + (dev->reg_count * 2); 
            int rx_len = port->recv(port, rx_buf, expected_len, dev->timeout_ms);

            bool poll_success = false;
            if (rx_len >= 5 && rx_buf[0] == dev->slave_id && rx_buf[1] == dev->func_code) {
                uint16_t calc_crc = utils_crc16_modbus(rx_buf, rx_len - 2);
                uint16_t recv_crc = rx_buf[rx_len - 2] | (rx_buf[rx_len - 1] << 8);
                if (calc_crc == recv_crc) {
                    execute_slice_mapping(dev, &rx_buf[3], rx_buf[2]);
                    poll_success = true;
                } else {
                    ESP_LOGW(TAG, "CRC Error on Port %d, Slave %d", port->port_id, dev->slave_id);
                }
            }

            // 更新状态灯与设备健康状态
            if (dev->status_tag_id > 0) {
                extern void reg_map_update_value(uint16_t tag_id, float value);
                reg_map_update_value(dev->status_tag_id, poll_success ? 1.0f : 0.0f);
            }
        }
        
        // 轮询间隔节流
        vTaskDelay(pdMS_TO_TICKS(dev->poll_interval_ms));
    }
}

// ============================================================
// 引擎分支 2：网络设备轮询引擎 (LwIP Wi-Fi + 硬件 W5100S)
// ============================================================
void protocol_engine_poll_network_cycle(sensor_device_t *sensors, int count) {
    if (!sensors || count <= 0) return;

    for (int i = 0; i < count; i++) {
        sensor_device_t *dev = &sensors[i];
        bool poll_success = false;

        // Modbus TCP 报文组装 (MBAP Header + PDU)
        uint8_t tx_buf[12];
        tx_buf[0] = 0x00; tx_buf[1] = 0x01; // Transaction ID
        tx_buf[2] = 0x00; tx_buf[3] = 0x00; // Protocol ID (0 = Modbus TCP)
        tx_buf[4] = 0x00; tx_buf[5] = 0x06; // Length (后面的字节数: ID+Func+Reg+Count = 6)
        tx_buf[6] = dev->slave_id;
        tx_buf[7] = dev->func_code;          
        tx_buf[8] = dev->start_reg >> 8; tx_buf[9] = dev->start_reg & 0xFF;
        tx_buf[10] = dev->reg_count >> 8; tx_buf[11] = dev->reg_count & 0xFF;

        if (dev->transport == 2) { 
            // ----------------------------------------------------
            // 链路 2：走 Wi-Fi 网络 (使用原生 LwIP sockets)
            // ----------------------------------------------------
            int sock = lwip_socket(AF_INET, SOCK_STREAM, 0);
            if (sock >= 0) {
                struct timeval timeout_tv;
                timeout_tv.tv_sec = dev->timeout_ms / 1000;
                timeout_tv.tv_usec = (dev->timeout_ms % 1000) * 1000;
                lwip_setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout_tv, sizeof(timeout_tv));
                lwip_setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout_tv, sizeof(timeout_tv));

                struct sockaddr_in dest_addr;
                dest_addr.sin_family = AF_INET;
                dest_addr.sin_port = lwip_htons(dev->target_port);
                dest_addr.sin_addr.s_addr = *(uint32_t *)dev->target_ip;

                if (lwip_connect(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr)) == 0) {
                    if (lwip_send(sock, tx_buf, 12, 0) == 12) {
                        uint8_t rx_buf[256];
                        int rx_len = lwip_recv(sock, rx_buf, sizeof(rx_buf), 0);
                        if (rx_len > 9 && rx_buf[6] == dev->slave_id && rx_buf[7] == dev->func_code) {
                            execute_slice_mapping(dev, &rx_buf[9], rx_buf[8]);
                            poll_success = true;
                        }
                    }
                } else {
                    ESP_LOGW(TAG, "Wi-Fi Socket connect failed to %d.%d.%d.%d", dev->target_ip[0], dev->target_ip[1], dev->target_ip[2], dev->target_ip[3]);
                }
                lwip_close(sock);
            }
        } 
        else if (dev->transport == 1) { 
            // ----------------------------------------------------
            // 链路 1：走 W5100S 硬件以太网 (寄存器级降维操作)
            // ----------------------------------------------------
            // 配置 WIZnet Socket 目标 IP 与端口
            setSn_DIPR(SOCK_W5100S_CLIENT, dev->target_ip);
            setSn_DPORT(SOCK_W5100S_CLIENT, dev->target_port);
            
            // 发起 TCP 连接
            setSn_MR(SOCK_W5100S_CLIENT, Sn_MR_TCP);
            setSn_CR(SOCK_W5100S_CLIENT, Sn_CR_OPEN);
            while(getSn_CR(SOCK_W5100S_CLIENT));

            setSn_CR(SOCK_W5100S_CLIENT, Sn_CR_CONNECT);
            while(getSn_CR(SOCK_W5100S_CLIENT));

            // 等待连接建立，加设超时锁
            int conn_wait = dev->timeout_ms;
            while(getSn_SR(SOCK_W5100S_CLIENT) != SOCK_ESTABLISHED && conn_wait > 0) {
                vTaskDelay(pdMS_TO_TICKS(10));
                conn_wait -= 10;
            }

            if (getSn_SR(SOCK_W5100S_CLIENT) == SOCK_ESTABLISHED) {
                wiz_send_data(SOCK_W5100S_CLIENT, tx_buf, 12);
                setSn_CR(SOCK_W5100S_CLIENT, Sn_CR_SEND); 
                while(getSn_CR(SOCK_W5100S_CLIENT));

                int rx_wait = dev->timeout_ms;
                while(getSn_RX_RSR(SOCK_W5100S_CLIENT) == 0 && rx_wait > 0) {
                    vTaskDelay(pdMS_TO_TICKS(10)); 
                    rx_wait -= 10;
                }

                int rx_len = getSn_RX_RSR(SOCK_W5100S_CLIENT);
                if (rx_len > 9) {
                    uint8_t rx_buf[256];
                    int read_len = (rx_len > 256) ? 256 : rx_len;
                    wiz_recv_data(SOCK_W5100S_CLIENT, rx_buf, read_len);
                    setSn_CR(SOCK_W5100S_CLIENT, Sn_CR_RECV); 
                    while(getSn_CR(SOCK_W5100S_CLIENT));
                    
                    if (rx_buf[6] == dev->slave_id && rx_buf[7] == dev->func_code) {
                        execute_slice_mapping(dev, &rx_buf[9], rx_buf[8]);
                        poll_success = true;
                    }
                }
            }
            
            // 清理硬件套接字
            setSn_CR(SOCK_W5100S_CLIENT, Sn_CR_DISCON);
            while(getSn_CR(SOCK_W5100S_CLIENT));
            setSn_CR(SOCK_W5100S_CLIENT, Sn_CR_CLOSE);
            while(getSn_CR(SOCK_W5100S_CLIENT));
        }

        // 更新状态灯与设备健康状态
        if (dev->status_tag_id > 0) {
            extern void reg_map_update_value(uint16_t tag_id, float value);
            reg_map_update_value(dev->status_tag_id, poll_success ? 1.0f : 0.0f);
        }

        vTaskDelay(pdMS_TO_TICKS(dev->poll_interval_ms));
    }
}