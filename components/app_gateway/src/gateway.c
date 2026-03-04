/**
 * @file gateway.c
 * @brief 应用层：网关引擎实现，负责装配底层硬件与中间件协议
 */
#include "gateway.h"
#include "bsp_uart.h"
#include "bsp_i2c.h"
#include "bsp_w5100s.h"

#include "register_map.h"
#include "modbus_master.h"
#include "sensor_parsers.h"
#include "io_manager.h"
#include "app_tcp_server.h"
#include "esp_log.h"

#include "gateway_tags.h"

static const char* TAG = "MAIN_GW";

// ==========================================
// 1. 硬件资源静态分配表 (采用 C99 指定初始化器，极致可读性)
// ==========================================
static const bsp_rs485_config_t master_port_conf = {
    .port_num   = 1,
    .tx_io_num  = 17,
    .rx_io_num  = 18,
    .rts_io_num = 19,
    .baud_rate  = 9600
};

static const bsp_rs485_config_t slave_port_conf = {
    .port_num   = 2,
    .tx_io_num  = 10,
    .rx_io_num  = 11,
    .rts_io_num = 12,
    .baud_rate  = 9600
};

static const bsp_i2c_config_t i2c_conf = {
    .port_num   = 0,
    .sda_io     = 4,
    .scl_io     = 5,
    .clk_speed  = 100000
};

// 针对杜邦线调试环境，SPI降频至 5MHz，硬件复位使用 GPIO 47
static const bsp_w5100s_config_t w5100s_conf = {
    .host_id         = SPI2_HOST,
    .mosi_io         = 13,
    .miso_io         = 14,
    .sclk_io         = 15,
    .cs_io           = 16,
    .rst_io          = 47,
    .clock_speed_mhz = 5
};

// ==========================================
// 2. Modbus 主机轮询对象字典
// ==========================================
static const sensor_device_t SENSOR_LIST[] = {
    {
        .name          = "Room1_TempHumi",
        .slave_id      = 1,
        .func_code     = 0x03,
        .start_reg     = 0x0000,
        .reg_count     = 2,
        .base_tag_id   = TAG_ID_ROOM1_TEMP, // 数据点: TAG_ID_ROOM1_TEMP(温度), TAG_ID_ROOM1_HUMI(湿度)
        .status_tag_id = TAG_ID_ROOM1_STATUS, // 【新增】：状态点: TAG_ID_ROOM1_STATUS (1=在线, 0=离线)
        .parse_func    = parser_standard_u16
    }
};
#define SENSOR_COUNT (sizeof(SENSOR_LIST)/sizeof(sensor_device_t))

// ==========================================
// 3. 核心调度钩子
// ==========================================
static void task_master_poll(void *arg) {
    modbus_master_init(master_port_conf.port_num);
    while (1) {
        modbus_master_poll_cycle(SENSOR_LIST, SENSOR_COUNT);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void gateway_init(void) {
    // 1. 硬件总线初始化
    bsp_rs485_init(&master_port_conf);
    bsp_rs485_init(&slave_port_conf);
    bsp_i2c_init(&i2c_conf);
    
    // 2. 数据中枢 (RTDB) 初始化
    reg_map_init();
    
    // 3. 动态构建数据字典 (定义本地路由 ID)
    reg_map_add_tag(TAG_ID_ROOM1_TEMP, "Room_Temp",    TAG_TYPE_FLOAT32, false);
    reg_map_add_tag(TAG_ID_ROOM1_HUMI, "Room_Humi",    TAG_TYPE_FLOAT32, false);
    reg_map_add_tag(TAG_ID_ROOM1_STATUS, "Room1_Status", TAG_TYPE_BOOL,    false);
    reg_map_add_tag(TAG_ID_LOCAL_RELAY_1, "PCF_Relay1", TAG_TYPE_BOOL,    true);
    reg_map_add_tag(TAG_ID_LOCAL_RELAY_2, "PCF_Relay2", TAG_TYPE_BOOL,    true);
    
    // 4. W5100S 纯硬件网络栈初始化
    bsp_w5100s_init(&w5100s_conf);
}

void gateway_start(void) {
    ESP_LOGI(TAG, "Starting Gateway Core Services...");
    
    io_manager_init();                                               // 启动本地 IO (PCF8574)
    xTaskCreate(task_master_poll, "gw_master", 4096, NULL, 5, NULL); // 启动下行 485 采集
    app_tcp_server_start();                                          // 启动上行 TCP 服务
}