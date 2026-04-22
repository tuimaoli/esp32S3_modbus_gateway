/**
 * @file modbus_slave.c
 * @brief 中间件层：Modbus RTU 从机引擎实现 (OOP 多态版)
 * @note 作为独立的 FreeRTOS 任务运行，通过 port->recv 阻塞，向上穿透至 RTDB
 */
#include "modbus_slave.h"
#include "register_map.h"
#include "utils.h"       
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "MB_SLAVE";

// 传递给 FreeRTOS 任务的参数包
typedef struct {
    bsp_serial_port_t *port;
    int slave_id;
} slave_task_args_t;

static void modbus_slave_task(void *arg) {
    slave_task_args_t *args = (slave_task_args_t *)arg;
    bsp_serial_port_t *port = args->port;
    int my_id = args->slave_id;
    free(args); // 释放传递进来的参数内存，防止内存泄漏

    uint8_t rx_buf[256];
    uint8_t tx_buf[256];

    ESP_LOGI(TAG, "Northbound Slave Task Started on Port ID: %d, Station ID: %d", port->port_id, my_id);

    while (1) {
        // 【核心解耦】只调用虚函数，完全不知道底层是原生串口还是 SC16IS750
        // 设置 100ms 超时让出 CPU
        int rx_len = port->recv(port, rx_buf, sizeof(rx_buf), 100);

        if (rx_len >= 8) { 
            // 1. 过滤非本机站号
            if (rx_buf[0] != my_id) continue;

            // 2. 严格 CRC 校验
            uint16_t calc_crc = utils_crc16_modbus(rx_buf, rx_len - 2);
            uint16_t recv_crc = rx_buf[rx_len - 2] | (rx_buf[rx_len - 1] << 8);

            if (calc_crc != recv_crc) {
                port->flush(port);
                continue;
            }

            uint8_t func_code = rx_buf[1];
            int tx_len = 0;

            // 3. 业务路由
            if (func_code == 0x03 || func_code == 0x04) {
                uint16_t start_addr = (rx_buf[2] << 8) | rx_buf[3];
                uint16_t quantity = (rx_buf[4] << 8) | rx_buf[5];

                if (quantity < 1 || quantity > 120) {
                    tx_buf[0] = my_id; tx_buf[1] = func_code | 0x80; tx_buf[2] = 0x03;
                    tx_len = 3;
                } else {
                    tx_buf[0] = my_id; tx_buf[1] = func_code; tx_buf[2] = quantity * 2;
                    tx_len = 3;
                    bool read_error = false;
                    for (int i = 0; i < quantity; i++) {
                        float val = 0; tag_quality_t qual;
                        // 穿透至中间件大脑 RTDB 读取
                        if (reg_map_get_value(start_addr + i, &val, &qual)) {
                            uint16_t int_val = (uint16_t)val;
                            tx_buf[tx_len++] = (int_val >> 8) & 0xFF; tx_buf[tx_len++] = int_val & 0xFF;
                        } else { 
                            read_error = true; break; 
                        }
                    }
                    if (read_error) { 
                        tx_buf[0] = my_id; tx_buf[1] = func_code | 0x80; tx_buf[2] = 0x02; 
                        tx_len = 3; 
                    }
                }
            } else if (func_code == 0x06) {
                uint16_t reg_addr = (rx_buf[2] << 8) | rx_buf[3];
                uint16_t write_val = (rx_buf[4] << 8) | rx_buf[5];
                // 穿透至 RTDB 写入，内部自动鉴权与记忆
                if (reg_map_write_value(reg_addr, (float)write_val)) {
                    memcpy(tx_buf, rx_buf, 6); tx_len = 6;
                } else {
                    tx_buf[0] = my_id; tx_buf[1] = func_code | 0x80; tx_buf[2] = 0x02; tx_len = 3;
                }
            } else {
                tx_buf[0] = my_id; tx_buf[1] = func_code | 0x80; tx_buf[2] = 0x01; tx_len = 3;
            }

            // 4. 追加 CRC 回送
            if (tx_len > 0) {
                uint16_t tx_crc = utils_crc16_modbus(tx_buf, tx_len);
                tx_buf[tx_len++] = tx_crc & 0xFF; tx_buf[tx_len++] = (tx_crc >> 8) & 0xFF;
                // 向上位机发送回包
                port->send(port, tx_buf, tx_len);
            }
        }
    }
}

void modbus_slave_start_worker(bsp_serial_port_t *port, int slave_id) {
    if (!port) return;
    slave_task_args_t *args = malloc(sizeof(slave_task_args_t));
    args->port = port;
    args->slave_id = slave_id;
    
    char task_name[16];
    snprintf(task_name, sizeof(task_name), "mb_slv_%d", port->port_id);
    
    // 优先级 5，保障北向响应的高实时性
    xTaskCreate(modbus_slave_task, task_name, 4096, args, 5, NULL);
}