/**
 * @file gateway.c
 * @brief 应用层：网关引擎核心实现 (动态组态终极版)
 */

#include "gateway.h"
#include "bsp_uart.h"
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

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "gateway_tags.h"

static const char* TAG = "MAIN_GW";

/* ============================================================
 * 1. 硬件资源静态分配表
 * ============================================================ */
static const bsp_rs485_config_t master_port_conf = {
    .port_num   = 1,
    .tx_io_num  = 17,
    .rx_io_num  = 18,
    .rts_io_num = 19,
    .baud_rate  = 9600
};

static const bsp_i2c_config_t i2c_conf = {
    .port_num   = 0,
    .sda_io     = 4,
    .scl_io     = 5,
    .clk_speed  = 100000
};

static const bsp_w5100s_config_t w5100s_conf = {
    .host_id         = SPI2_HOST,
    .mosi_io         = 13,
    .miso_io         = 14,
    .sclk_io         = 15,
    .cs_io           = 16,
    .rst_io          = 47,
    .clock_speed_mhz = 5
};

static const bsp_wifi_config_t wifi_conf = {
    .ssid     = "DD_GATEWAY1",
    .password = "ddzn1811"
};

/* ============================================================
 * 2. 动态轮询任务核心
 * ============================================================ */
static sensor_device_t *g_dynamic_sensors = NULL;
static int g_dynamic_sensor_count = 0;

static void task_master_poll(void *arg) {
    // 调用新引擎初始化
    protocol_engine_init(master_port_conf.port_num);
    
    // 【解耦关键】在此处由应用大管家将 MQTT 发送队列钩子挂载到底层引擎！
    protocol_engine_register_data_cb(app_mqtt_enqueue_data);
    
    while (1) {
        if (g_dynamic_sensor_count > 0 && g_dynamic_sensors != NULL) {
            protocol_engine_poll_cycle(g_dynamic_sensors, g_dynamic_sensor_count);
        } else {
            ESP_LOGW(TAG, "No sensors configured. Waiting for JSON push via WebUI...");
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

/* ============================================================
 * 3. 网关生命周期控制接口
 * ============================================================ */
void gateway_init(void) {
    bsp_fs_init();
    bsp_rs485_init(&master_port_conf);
    bsp_i2c_init(&i2c_conf);
    bsp_wifi_init(&wifi_conf);
    bsp_w5100s_init(&w5100s_conf);
    
    reg_map_init();
    
    reg_map_add_tag(TAG_ID_LOCAL_RELAY_1, "PCF_Relay1", TAG_TYPE_BOOL, true);
    reg_map_add_tag(TAG_ID_LOCAL_RELAY_2, "PCF_Relay2", TAG_TYPE_BOOL, true);

    config_manager_load(&g_dynamic_sensors, &g_dynamic_sensor_count);
}

void gateway_start(void) {
    ESP_LOGI(TAG, "Starting Gateway Core Services...");
    
    io_manager_init();                                               
    xTaskCreate(task_master_poll, "gw_master", 6144, NULL, 5, NULL); 
    app_tcp_server_start();                                          
    
    app_webserver_start();
    app_sntp_init();
    app_mqtt_start(g_dynamic_sensors, g_dynamic_sensor_count);
}