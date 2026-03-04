/**
 * @file gateway.c
 * @brief 应用层：网关引擎核心实现 (融合 RS485、W5100S、Wi-Fi 三大物理层)
 */

#include "gateway.h"
#include "bsp_uart.h"
#include "bsp_i2c.h"
#include "bsp_w5100s.h"
#include "bsp_wifi.h"       // 引入新加入的 Wi-Fi 驱动

#include "register_map.h"
#include "modbus_master.h"
#include "modbus_template.h"
#include "io_manager.h"
#include "app_tcp_server.h"
#include "esp_log.h"
#include "gateway_tags.h"

#define TAG_ID_ROOM2_STATUS  200
#define TAG_ID_POWER_VOLTAGE 201
#define TAG_ID_ROOM3_STATUS  300
#define TAG_ID_WIFI_SENSOR_1 301

static const char* TAG = "MAIN_GW";

/* ============================================================
 * 1. 硬件资源静态分配表
 * ============================================================ */
// ... (此处保留原有的 master_port_conf, i2c_conf, w5100s_conf 不变) ...
static const bsp_rs485_config_t master_port_conf = { .port_num = 1, .tx_io_num = 17, .rx_io_num = 18, .rts_io_num = 19, .baud_rate = 9600 };
static const bsp_i2c_config_t i2c_conf = { .port_num = 0, .sda_io = 4, .scl_io = 5, .clk_speed = 100000 };
static const bsp_w5100s_config_t w5100s_conf = { .host_id = SPI2_HOST, .mosi_io = 13, .miso_io = 14, .sclk_io = 15, .cs_io = 16, .rst_io = 47, .clock_speed_mhz = 5 };

// 新增 Wi-Fi 账号密码配置
static const bsp_wifi_config_t wifi_conf = {
    .ssid     = "Factory_IOT_Net",
    .password = "Admin12345"
};

/* ============================================================
 * 2. 业务映射规则 (复用通用解析引擎)
 * ============================================================ */
static modbus_mapping_rule_t room1_rules[] = { { .target_tag_id = TAG_ID_ROOM1_TEMP, .byte_offset = 0, .type = MB_TYPE_INT16_AB, .scale = 0.1f } };
static const sensor_profile_t room1_profile = { .rule_count = 1, .mapping_rules = room1_rules };
static void parser_room1_wrapper(const uint8_t *rx_buf, uint16_t len, uint16_t base_tag_id) {
    if (len >= 5) modbus_universal_parser(&rx_buf[3], rx_buf[2], &room1_profile);
}

// 给厂区 Wi-Fi 传感器加个规则
static modbus_mapping_rule_t wifi_sensor_rules[] = { { .target_tag_id = TAG_ID_WIFI_SENSOR_1, .byte_offset = 0, .type = MB_TYPE_UINT16_AB, .scale = 1.0f } };
static const sensor_profile_t wifi_sensor_profile = { .rule_count = 1, .mapping_rules = wifi_sensor_rules };
static void parser_wifi_wrapper(const uint8_t *rx_buf, uint16_t len, uint16_t base_tag_id) {
    if (len >= 5) modbus_universal_parser(&rx_buf[3], rx_buf[2], &wifi_sensor_profile);
}

/* ============================================================
 * 3. 三模态传感器组态列表！
 * ============================================================ */
static const sensor_device_t SENSOR_LIST[] = {
    {
        .name          = "Node1_RS485",
        .transport     = MB_TRANSPORT_RTU,      // 物理层：RS485 串口
        .slave_id      = 1, .func_code = 0x03, .start_reg = 0x00, .reg_count = 1,
        .status_tag_id = TAG_ID_ROOM1_STATUS, .parse_func = parser_room1_wrapper
    },
    {
        .name          = "Node2_W5100S",
        .transport     = MB_TRANSPORT_TCP_W5100S, // 物理层：W5100S SPI 硬以太网
        .target_ip     = {192, 168, 1, 100}, .target_port = 502,
        .slave_id      = 1, .func_code = 0x03, .start_reg = 0x0100, .reg_count = 2,
        .status_tag_id = TAG_ID_ROOM2_STATUS, .parse_func = parser_room1_wrapper 
    },
    {
        .name          = "Node3_WiFi",
        .transport     = MB_TRANSPORT_TCP_WIFI,   // 物理层：ESP32 Wi-Fi 软以太网
        .target_ip     = {192, 168, 0, 50},  .target_port = 502,
        .slave_id      = 1, .func_code = 0x03, .start_reg = 0x00, .reg_count = 1,
        .status_tag_id = TAG_ID_ROOM3_STATUS, .parse_func = parser_wifi_wrapper
    }
};
#define SENSOR_COUNT (sizeof(SENSOR_LIST)/sizeof(sensor_device_t))

static void task_master_poll(void *arg) {
    modbus_master_init(master_port_conf.port_num);
    while (1) {
        modbus_master_poll_cycle(SENSOR_LIST, SENSOR_COUNT);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void gateway_init(void) {
    bsp_rs485_init(&master_port_conf);
    bsp_i2c_init(&i2c_conf);
    
    // 启动 LwIP 软协议栈及 Wi-Fi
    bsp_wifi_init(&wifi_conf);
    
    // 启动 W5100S 硬协议栈
    bsp_w5100s_init(&w5100s_conf);
    
    reg_map_init();
    reg_map_add_tag(TAG_ID_ROOM1_TEMP,    "Room_Temp",    TAG_TYPE_FLOAT32, false);
    reg_map_add_tag(TAG_ID_ROOM1_STATUS,  "Room1_Status", TAG_TYPE_BOOL,    false);
    reg_map_add_tag(TAG_ID_ROOM3_STATUS,  "Room3_Status", TAG_TYPE_BOOL,    false);
    reg_map_add_tag(TAG_ID_WIFI_SENSOR_1, "WiFi_Data",    TAG_TYPE_FLOAT32, false);
}

void gateway_start(void) {
    ESP_LOGI(TAG, "Starting Gateway Core Services...");
    xTaskCreate(task_master_poll, "gw_master", 5120, NULL, 5, NULL); 
    app_tcp_server_start();                                          
}