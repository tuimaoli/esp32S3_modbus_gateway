/**
 * @file gateway.c
 * @brief 应用层：网关引擎核心实现 (动态组态终极版)
 * @note 彻底删除了硬编码的 SENSOR_LIST，改为由 config_manager 从 LittleFS 动态拉取加载
 */

#include "gateway.h"
#include "bsp_uart.h"
#include "bsp_i2c.h"
#include "bsp_w5100s.h"
#include "bsp_wifi.h"
#include "bsp_fs.h"

#include "register_map.h"
#include "config_manager.h"
#include "protocol_engine.h"  // 架构演进：引入全新泛化调度引擎
#include "app_tcp_server.h"
#include "app_webserver.h"
#include "io_manager.h"

// V2.0 引入云端通讯与校时组件
#include "app_sntp.h"
#include "app_mqtt.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "gateway_tags.h" // 仅保留本地 IO 等底层固定测点

static const char* TAG = "MAIN_GW";

/* ============================================================
 * 1. 硬件资源静态分配表 (纯底层物理配置)
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
    
    while (1) {
        // 将动态组态配置丢给泛化引擎处理
        if (g_dynamic_sensor_count > 0 && g_dynamic_sensors != NULL) {
            protocol_engine_poll_cycle(g_dynamic_sensors, g_dynamic_sensor_count);
        } else {
            ESP_LOGW(TAG, "No sensors configured. Waiting for JSON push via WebUI...");
        }
        vTaskDelay(pdMS_TO_TICKS(500)); // 缩短大循环死区，交给引擎内部进行精确毫秒调度
    }
}

/* ============================================================
 * 3. 网关生命周期控制接口
 * ============================================================ */
void gateway_init(void) {
    // 1. 挂载文件系统 (最高优先级)
    bsp_fs_init();

    // 2. 初始化底层硬件总线与协议栈
    bsp_rs485_init(&master_port_conf);
    bsp_i2c_init(&i2c_conf);
    bsp_wifi_init(&wifi_conf);
    bsp_w5100s_init(&w5100s_conf);
    
    // 3. 初始化实时数据库
    reg_map_init();
    
    // 4. 注册网关自身固定的逻辑点 (如本地 IO)
    reg_map_add_tag(TAG_ID_LOCAL_RELAY_1, "PCF_Relay1", TAG_TYPE_BOOL, true);
    reg_map_add_tag(TAG_ID_LOCAL_RELAY_2, "PCF_Relay2", TAG_TYPE_BOOL, true);

    // 5. 组态灵魂：从 config.json 动态加载配置表，并向 RTDB 自动注册所有传感器测点！
    config_manager_load(&g_dynamic_sensors, &g_dynamic_sensor_count);
}

void gateway_start(void) {
    ESP_LOGI(TAG, "Starting Gateway Core Services...");
    
    io_manager_init();                                               
    xTaskCreate(task_master_poll, "gw_master", 6144, NULL, 5, NULL); 
    app_tcp_server_start();                                          

    // =========================================================
    // 架构优化：无条件启动高层业务组件
    // ESP-IDF 的 HTTPD、SNTP 和 MQTT 客户端内部自带状态机守护
    // 即便当前没有 IP，它们也会自动在后台静默等待或尝试重连，不应被强制阻塞
    // =========================================================
    
    // 启动 80 端口 Web 可视化组态后台
    app_webserver_start();
    
    // 启动 NTP 网络自动校时 (无网时将返回 1970 纪元，可后期引入 Web 或 MQTT 离线校时)
    app_sntp_init();
    
    // 启动 MQTT 客户端与异步发送队列
    app_mqtt_start(g_dynamic_sensors, g_dynamic_sensor_count);
}