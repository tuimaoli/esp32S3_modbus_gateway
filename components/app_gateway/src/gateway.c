/**
 * @file gateway.c
 * @brief 应用层：网关引擎核心实现 (OOP 多串口并发 + 网络设备双支持架构)
 */

#include <string.h>
#include <stdlib.h>
#include "gateway.h"
#include "bsp_serial_port.h"
#include "bsp_uart_native.h"
#include "bsp_sc16is750.h"
#include "bsp_i2c.h"
#include "bsp_w5100s.h"
#include "bsp_wifi.h"
#include "bsp_fs.h"
#include "register_map.h"
#include "config_manager.h"
#include "protocol_engine.h"
#include "app_tcp_server.h"
#include "app_webserver.h"
#include "io_manager.h"
#include "app_sntp.h"
#include "app_mqtt.h"
#include "app_linkage.h"
#include "modbus_slave.h" 

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char* TAG = "MAIN_GW";

// 静态板载硬件分配
static const bsp_i2c_config_t i2c_conf = { .port_num = 0, .sda_io = 4, .scl_io = 5, .clk_speed = 100000 };
static const bsp_w5100s_config_t w5100s_conf = { .host_id = SPI2_HOST, .mosi_io = 13, .miso_io = 14, .sclk_io = 15, .cs_io = 16, .rst_io = 6, .clock_speed_mhz = 5 };
static const bsp_wifi_config_t wifi_conf = { .ssid = "DD_GATEWAY1", .password = "ddzn1811" };

static sensor_device_t *g_dynamic_sensors = NULL;
static int g_dynamic_sensor_count = 0;

// ============================================================
// 独立轮询任务包裹器
// ============================================================

// A. 本地串口轮询线程传参
typedef struct {
    bsp_serial_port_t *port;
    sensor_device_t *sensors;
    int count;
} master_task_args_t;

// 获取特定逻辑串口挂载的设备
static sensor_device_t* get_sensors_for_serial_port(int port_id, int *out_count) {
    sensor_device_t *binds = malloc(sizeof(sensor_device_t) * g_dynamic_sensor_count);
    int c = 0;
    for(int i=0; i<g_dynamic_sensor_count; i++) {
        // transport=0表示串口链路，bind_port_id为具体逻辑端口
        if(g_dynamic_sensors[i].transport == 0 && g_dynamic_sensors[i].bind_port_id == port_id) { 
            memcpy(&binds[c++], &g_dynamic_sensors[i], sizeof(sensor_device_t));
        }
    }
    *out_count = c;
    return binds;
}

static void master_serial_poll_task_worker(void *arg) {
    master_task_args_t *args = (master_task_args_t *)arg;
    while(1) {
        if (args->count > 0 && args->sensors != NULL) {
            // Note: 引擎层已升维支持 OOP Port
            protocol_engine_poll_serial_cycle(args->port, args->sensors, args->count);
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

// B. 获取所有网络设备 (TCP/Wi-Fi)
static sensor_device_t* get_network_sensors(int *out_count) {
    sensor_device_t *binds = malloc(sizeof(sensor_device_t) * g_dynamic_sensor_count);
    int c = 0;
    for(int i=0; i<g_dynamic_sensor_count; i++) {
        if(g_dynamic_sensors[i].transport == 1 || g_dynamic_sensors[i].transport == 2) { 
            memcpy(&binds[c++], &g_dynamic_sensors[i], sizeof(sensor_device_t));
        }
    }
    *out_count = c;
    return binds;
}

// 独立的网络 TCP Modbus 轮询线程
static void master_network_poll_task_worker(void *arg) {
    int count = 0;
    sensor_device_t *net_sensors = get_network_sensors(&count);
    ESP_LOGI(TAG, "=> Spawning Network Poller Task (%d IP devices)", count);

    while(1) {
        if (count > 0 && net_sensors != NULL) {
            protocol_engine_poll_network_cycle(net_sensors, count);
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// ============================================================
// 生命周期控制
// ============================================================
void gateway_init(void) {
    ESP_LOGI(TAG, "Hardware & BSP Initialization...");
    bsp_fs_init();
    bsp_i2c_init(&i2c_conf); 
    bsp_wifi_init(&wifi_conf);
    bsp_w5100s_init(&w5100s_conf);
    
    reg_map_init();
    config_manager_load(&g_dynamic_sensors, &g_dynamic_sensor_count);

    const gateway_config_t* gw_cfg = config_manager_get_gw_cfg();
    if (gw_cfg && strlen(gw_cfg->device_id) > 0) bsp_wifi_set_device_id(gw_cfg->device_id);
}

void gateway_start(void) {
    ESP_LOGI(TAG, "Starting Factory: Network & OOP Serial Devices...");
    
    // 挂载数据流钩子
    protocol_engine_register_data_cb(app_mqtt_enqueue_data);

    const gateway_config_t* gw_cfg = config_manager_get_gw_cfg();
    
    // 1. 启动硬件串口多态引擎分发
    for (int i = 0; i < gw_cfg->port_count; i++) {
        const serial_port_cfg_t *pcfg = &gw_cfg->ports[i];
        bsp_serial_port_t *port_obj = NULL;

        if (pcfg->hw_type == PORT_HW_NATIVE) {
            port_obj = bsp_native_uart_create(pcfg->port_id, pcfg->native_uart_num, pcfg->tx_pin, pcfg->rx_pin, pcfg->rts_pin, pcfg->baud_rate);
        } else if (pcfg->hw_type == PORT_HW_SC16IS750) {
            port_obj = bsp_sc16is750_create(pcfg->port_id, 0, pcfg->i2c_addr, pcfg->baud_rate, 14745600);
        }

        if (port_obj) {
            if (pcfg->role == PORT_ROLE_MASTER) {
                int bind_cnt = 0;
                sensor_device_t *bind_sensors = get_sensors_for_serial_port(pcfg->port_id, &bind_cnt);
                
                master_task_args_t *m_args = malloc(sizeof(master_task_args_t));
                m_args->port = port_obj; m_args->sensors = bind_sensors; m_args->count = bind_cnt;
                
                char task_name[16]; snprintf(task_name, sizeof(task_name), "mst_srl_%d", port_obj->port_id);
                xTaskCreate(master_serial_poll_task_worker, task_name, 6144, m_args, 5, NULL);
                ESP_LOGI(TAG, "=> Port %d Thread Spawns (%d Local RTU Nodes)", port_obj->port_id, bind_cnt);
                
            } else if (pcfg->role == PORT_ROLE_SLAVE) {
                modbus_slave_start_worker(port_obj, pcfg->slave_id);
                ESP_LOGI(TAG, "=> Port %d Thread Spawns (Slave Mode ID: %d)", port_obj->port_id, pcfg->slave_id);
            }
        }
    }

    // 2. 独立启动网络设备轮询守护进程 (Wi-Fi/Ethernet)
    xTaskCreate(master_network_poll_task_worker, "mst_net", 6144, NULL, 5, NULL);

    // 3. 常规子系统点火
    io_manager_init();                                               
    app_tcp_server_start();                                          
    app_webserver_start();
    app_sntp_init();
    app_mqtt_start(g_dynamic_sensors, g_dynamic_sensor_count);
    app_linkage_start();
    
    ESP_LOGI(TAG, "Gateway Core Fusion Engine fully operational!");
}