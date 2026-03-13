/**
 * @file main.c
 * @brief ESP32-S3 工业边缘物联网网关 - 系统主入口 (V4.0)
 * @note 遵循极简入口原则，仅负责全局前置环境配置（日志隔离）与大管家移交
 */

#include "esp_log.h"
#include "gateway.h"

static const char *TAG = "APP_MAIN";

/**
 * @brief 全局日志等级精细化控制中心
 * @note 方便开发期调试，防止控制台被无效信息淹没
 */
static void system_log_level_setup(void) {
    // 1. 全局默认底噪控制 (屏蔽底层 Wi-Fi、LwIP 的冗余 Debug 信息)
    esp_log_level_set("*", ESP_LOG_INFO);       
    
    // 2. 核心中间件：微型实时数据库与规则引擎 (按需开启 DEBUG)
    esp_log_level_set("RTDB", ESP_LOG_INFO);
    esp_log_level_set("LINKAGE", ESP_LOG_DEBUG); // 调试边缘联动逻辑时设为 DEBUG
    
    // 3. 通信与调度层
    esp_log_level_set("PROTO_ENG", ESP_LOG_INFO); // 协议轮询引擎
    esp_log_level_set("APP_MQTT", ESP_LOG_INFO);  // 云端通讯
    esp_log_level_set("WEB_API", ESP_LOG_INFO);   // 融合前端与 API
    esp_log_level_set("IO_MGR", ESP_LOG_INFO);    // 本地物理外设
    
    // 4. BSP 驱动底层 (通常量产或排查物理故障时才开)
    esp_log_level_set("BSP_WIFI", ESP_LOG_WARN);  
}

void app_main(void) {
    // 1. 挂载日志过滤系统
    system_log_level_setup();

    ESP_LOGI(TAG, "==================================================");
    ESP_LOGI(TAG, "   IoT Edge Gateway V4.0 Boot Sequence Started    ");
    ESP_LOGI(TAG, "==================================================");

    // 2. 唤醒应用层大管家：初始化所有外设、中间件与配置文件
    gateway_init();

    // 3. 点火：启动所有后台守护任务与数据流转机制
    gateway_start();
    
    ESP_LOGI(TAG, "Boot Sequence Complete. Yielding Main Task.");
    
    // main 任务跑完即销毁，把 CPU 彻底让给 FreeRTOS 的调度器
}