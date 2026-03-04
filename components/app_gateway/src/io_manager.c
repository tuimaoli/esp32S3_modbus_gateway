/**
 * @file io_manager.c
 * @brief 应用层：本地 IO 与 PCF8574 管理
 * @note 负责硬件状态与数据中枢 (RTDB) 的双向同步
 */

#include "io_manager.h"
#include "bsp_i2c.h"
#include "register_map.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "gateway_tags.h"

#define PCF8574_ADDR 0x20
static const char *TAG = "IO_MGR";

static void io_poll_task(void *arg) 
{
    uint8_t last_input = 0xFF;
    
    while (1) {
        // ==========================================
        // 1. 读取硬件引脚状态 -> 更新至 RTDB 数据中枢
        // ==========================================
        uint8_t current_state = 0;
        esp_err_t ret = bsp_i2c_read(0, PCF8574_ADDR, &current_state, 1);
        
        if (ret == ESP_OK) {
            if (current_state != last_input) {
                last_input = current_state;
                reg_map_update_value(500, (float)current_state);
            }
        }

        // ==========================================
        // 2. 从 RTDB 获取输出指令 -> 下发至控制硬件
        // ==========================================
        float out1_val = 0.0f;
        float out2_val = 0.0f;
        tag_quality_t q1, q2;
        
        // 默认掩码：保持当前输入引脚的原有电平
        uint8_t out_mask = current_state; 
        
        // 判断继电器 1 (映射到 bit 4)
        if (reg_map_get_value(TAG_ID_LOCAL_RELAY_1, &out1_val, &q1) && (q1 == TAG_QUAL_GOOD)) {
            if (out1_val > 0.5f) {
                out_mask |= (1 << 4); 
            } else {
                out_mask &= ~(1 << 4);
            }
        }
        
        // 判断继电器 2 (映射到 bit 5)
        if (reg_map_get_value(TAG_ID_LOCAL_RELAY_2, &out2_val, &q2) && (q2 == TAG_QUAL_GOOD)) {
            if (out2_val > 0.5f) {
                out_mask |= (1 << 5); 
            } else {
                out_mask &= ~(1 << 5);
            }
        }

        // 将合并后的状态掩码写回 PCF8574
        bsp_i2c_write(0, PCF8574_ADDR, &out_mask, 1);
        
        // 延时让出 CPU
        vTaskDelay(pdMS_TO_TICKS(100)); 
    }
}

void io_manager_init(void) 
{
    xTaskCreate(io_poll_task, "io_mgr_task", 3072, NULL, 5, NULL);
    ESP_LOGI(TAG, "Local IO Manager started.");
}