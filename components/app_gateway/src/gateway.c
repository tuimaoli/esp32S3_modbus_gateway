/**
 * @file gateway.c
 * @brief 应用层：网关引擎核心实现
 * @note 组装底层硬件驱动 (BSP) 与中间件 (Middleware)，基于数据驱动模型配置传感器业务
 */

#include "gateway.h"
#include "bsp_uart.h"
#include "bsp_i2c.h"
#include "bsp_w5100s.h"

#include "register_map.h"
#include "modbus_master.h"
#include "modbus_template.h"
#include "io_manager.h"
#include "app_tcp_server.h"
#include "esp_log.h"
#include "gateway_tags.h"

static const char* TAG = "MAIN_GW";

/* ============================================================
 * 1. 硬件资源静态分配表 (BSP 层配置)
 * ========================================== */
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

static const bsp_w5100s_config_t w5100s_conf = {
    .host_id         = SPI2_HOST,
    .mosi_io         = 13,
    .miso_io         = 14,
    .sclk_io         = 15,
    .cs_io           = 16,
    .rst_io          = 47,
    .clock_speed_mhz = 5
};

/* ============================================================
 * 2. 业务层：数据驱动的 Modbus 映射规则字典 (取代硬编码解析)
 * ============================================================ */

/** * @brief 房间 1 温湿度传感器的“字节萃取”规则表
 * @note 假设传感器返回 2 个寄存器(4字节)，全是 16 位有符号整数，真实值需乘以 0.1
 */
static modbus_mapping_rule_t room1_rules[] = {
    {
        .target_tag_id = TAG_ID_ROOM1_TEMP, 
        .byte_offset   = 0,                 // 从 payload 第 0 字节开始取
        .bit_offset    = 0,
        .type          = MB_TYPE_INT16_AB,  // 标准大端 16 位
        .scale         = 0.1f               // 缩放系数：除以 10
    },
    {
        .target_tag_id = TAG_ID_ROOM1_HUMI, 
        .byte_offset   = 2,                 // 从 payload 第 2 字节开始取
        .bit_offset    = 0,
        .type          = MB_TYPE_INT16_AB, 
        .scale         = 0.1f
    }
};

/** @brief 房间 1 传感器画像实例 */
static const sensor_profile_t room1_profile = {
    .name          = "Room1_Env_Sensor",
    .slave_id      = 1,
    .func_code     = 0x03,
    .start_reg     = 0x0000,
    .reg_count     = 2,
    .status_tag_id = TAG_ID_ROOM1_STATUS,
    .rule_count    = sizeof(room1_rules) / sizeof(room1_rules[0]),
    .mapping_rules = room1_rules,
    .next          = NULL
};

/**
 * @brief 桥接函数：剥离 Modbus 协议头，将纯数据 Payload 喂给通用解析引擎
 */
static void parser_room1_wrapper(const uint8_t *rx_buf, uint16_t len, uint16_t base_tag_id) {
    if (len < 5) return; // 防御性拦截：长度非法
    
    uint8_t byte_count = rx_buf[2];
    if (len < (3 + byte_count + 2)) return; // 防御性拦截：报文不完整

    // 原始报文格式: [ID] [Func] [ByteCount] [Data...] [CRC_L] [CRC_H]
    // 纯数据 payload 从 rx_buf[3] 开始
    modbus_universal_parser(&rx_buf[3], byte_count, &room1_profile);
}

/* ============================================================
 * 3. 任务与调度分配表
 * ============================================================ */

/** @brief 下发给轮询引擎的任务清单 */
static const sensor_device_t SENSOR_LIST[] = {
    {
        .name          = "Room1_Env_Node",
        .slave_id      = room1_profile.slave_id,
        .func_code     = room1_profile.func_code,
        .start_reg     = room1_profile.start_reg,
        .reg_count     = room1_profile.reg_count,
        .base_tag_id   = 0, // 在通用模板引擎中已弃用该字段，由映射表接管
        .status_tag_id = room1_profile.status_tag_id,
        .parse_func    = parser_room1_wrapper
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

/* ============================================================
 * 4. 网关生命周期控制接口
 * ============================================================ */

void gateway_init(void) {
    // 1. 硬件总线初始化
    bsp_rs485_init(&master_port_conf);
    bsp_rs485_init(&slave_port_conf);
    bsp_i2c_init(&i2c_conf);
    
    // 2. 数据中枢 (RTDB) 初始化
    reg_map_init();
    
    // 3. 动态构建数据字典 (注册逻辑 ID，替代魔法数字)
    reg_map_add_tag(TAG_ID_ROOM1_TEMP,    "Room_Temp",    TAG_TYPE_FLOAT32, false);
    reg_map_add_tag(TAG_ID_ROOM1_HUMI,    "Room_Humi",    TAG_TYPE_FLOAT32, false);
    reg_map_add_tag(TAG_ID_ROOM1_STATUS,  "Room1_Status", TAG_TYPE_BOOL,    false);
    reg_map_add_tag(TAG_ID_LOCAL_RELAY_1, "PCF_Relay1",   TAG_TYPE_BOOL,    true);
    reg_map_add_tag(TAG_ID_LOCAL_RELAY_2, "PCF_Relay2",   TAG_TYPE_BOOL,    true);
    
    // 4. W5100S 纯硬件网络栈初始化
    bsp_w5100s_init(&w5100s_conf);
}

void gateway_start(void) {
    ESP_LOGI(TAG, "Starting Gateway Core Services...");
    
    io_manager_init();                                               // 启动本地 IO (PCF8574)
    xTaskCreate(task_master_poll, "gw_master", 4096, NULL, 5, NULL); // 启动下行 485 采集
    app_tcp_server_start();                                          // 启动上行 TCP 服务
}