/**
 * @file io_manager.c
 * @brief 应用层：本地 IO、按键监控与指示灯控制 (V4.1 动态标定版)
 * @note 彻底消除继电器与ADC硬编码参数，全面接入 V4.1 动态组态映射
 */

#include "io_manager.h"
#include "bsp_i2c.h"
#include "bsp_ads1115.h"
#include "bsp_wifi.h" 
#include "register_map.h"
#include "config_manager.h"  // ⚡ 必须包含配置管家
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "driver/gpio.h"
#include "gateway_tags.h"
#include <unistd.h>
#include <string.h>

#define PCF8574_ADDR 0x20
static const char *TAG = "IO_MGR";

#define GPIO_BTN_RESET  0   
#define GPIO_LED_1      10  
#define GPIO_LED_2      11  
#define GPIO_LED_3      12  

// ========================================================
// V4.2 架构升维：本地测点出厂静态数据字典 (消除一切注册硬编码)
// ========================================================
typedef struct {
    uint16_t tag_id;
    const char *name;
    tag_type_t type;
    bool writable;      // 北向是否可控
} local_io_dict_t;

static const local_io_dict_t s_local_io_dict[] = {
    // 扩展板 IO
    { TAG_ID_LOCAL_RELAY_1,   "PCF_Relay1",    TAG_TYPE_BOOL,    true  },
    { TAG_ID_LOCAL_RELAY_2,   "PCF_Relay2",    TAG_TYPE_BOOL,    true  },
    { TAG_ID_LOCAL_IO_INPUTS, "PCF_Inputs",    TAG_TYPE_INT32,   false },
    // 模拟量通道
    { TAG_ID_LOCAL_AIN_0,     "Local_AIN_0",   TAG_TYPE_FLOAT32, false },
    { TAG_ID_LOCAL_AIN_1,     "Local_AIN_1",   TAG_TYPE_FLOAT32, false },
    { TAG_ID_LOCAL_AIN_2,     "Local_AIN_2",   TAG_TYPE_FLOAT32, false },
    { TAG_ID_LOCAL_AIN_3,     "Local_AIN_3",   TAG_TYPE_FLOAT32, false },
    // 系统级指示灯与按键
    { TAG_ID_SYS_BTN_RESET,   "SYS_Btn_Reset", TAG_TYPE_BOOL,    false },
    { TAG_ID_SYS_LED_1,       "SYS_LED_1",     TAG_TYPE_BOOL,    false },
    { TAG_ID_SYS_LED_2,       "SYS_LED_2",     TAG_TYPE_BOOL,    false },
    { TAG_ID_SYS_LED_3,       "SYS_LED_3",     TAG_TYPE_BOOL,    false }
};

/**
 * @brief 内部辅助函数：根据测点 ID 极速获取动态配置的 Scale/Offset/Persist
 */
static bool get_local_tag_cfg(uint16_t tag_id, bool *persist, float *scale, float *offset) {
    const gateway_config_t *gw_cfg = config_manager_get_gw_cfg();
    if (persist) *persist = false;
    if (scale) *scale = 1.0f;
    if (offset) *offset = 0.0f;
    if (!gw_cfg) return false;
    
    for (int i = 0; i < gw_cfg->local_tag_count; i++) {
        if (gw_cfg->local_tags[i].tag_id == tag_id) {
            if (persist) *persist = gw_cfg->local_tags[i].persist;
            if (scale) *scale = gw_cfg->local_tags[i].scale;
            if (offset) *offset = gw_cfg->local_tags[i].offset;
            return true;
        }
    }
    return false;
}

static void io_poll_task(void *arg) 
{
    uint8_t last_input = 0xFF;
    int btn_press_ms = 0;
    int tick_count = 0;
    
    while (1) {
        // ==========================================
        // 1. 物理按键与指示灯处理
        // ==========================================
        if (gpio_get_level(GPIO_BTN_RESET) == 0) {
            btn_press_ms += 100;
            reg_map_update_value(TAG_ID_SYS_BTN_RESET, 1.0f);
            
            if (btn_press_ms >= 5000) {
                ESP_LOGW(TAG, "Factory Reset Triggered!");
                unlink("/vfs/wifi.json"); 
                for (int i = 0; i < 5; i++) {
                    gpio_set_level(GPIO_LED_1, 1); gpio_set_level(GPIO_LED_2, 1); gpio_set_level(GPIO_LED_3, 1);
                    vTaskDelay(pdMS_TO_TICKS(100));
                    gpio_set_level(GPIO_LED_1, 0); gpio_set_level(GPIO_LED_2, 0); gpio_set_level(GPIO_LED_3, 0);
                    vTaskDelay(pdMS_TO_TICKS(100));
                }
                esp_restart(); 
            }
        } else {
            btn_press_ms = 0;
            reg_map_update_value(TAG_ID_SYS_BTN_RESET, 0.0f);
        }

        if (bsp_wifi_is_ap_mode()) {
            if (tick_count % 2 == 0) { 
                int step = (tick_count / 2) % 3;
                reg_map_update_value(TAG_ID_SYS_LED_1, step == 0 ? 1.0f : 0.0f);
                reg_map_update_value(TAG_ID_SYS_LED_2, step == 1 ? 1.0f : 0.0f);
                reg_map_update_value(TAG_ID_SYS_LED_3, step == 2 ? 1.0f : 0.0f);
            }
        }

        float l1 = 0, l2 = 0, l3 = 0;
        reg_map_get_value(TAG_ID_SYS_LED_1, &l1, NULL);
        reg_map_get_value(TAG_ID_SYS_LED_2, &l2, NULL);
        reg_map_get_value(TAG_ID_SYS_LED_3, &l3, NULL);
        gpio_set_level(GPIO_LED_1, l1 > 0.5f);
        gpio_set_level(GPIO_LED_2, l2 > 0.5f);
        gpio_set_level(GPIO_LED_3, l3 > 0.5f);

        // ==========================================
        // 2. PCF8574 扩展 IO 同步逻辑
        // ==========================================
        uint8_t current_state = 0;
        if (bsp_i2c_read(0, PCF8574_ADDR, &current_state, 1) == ESP_OK) {
            if (current_state != last_input) {
                last_input = current_state;
                reg_map_update_value(TAG_ID_LOCAL_IO_INPUTS, (float)current_state);
            }
        }

        float out1_val = 0.0f; float out2_val = 0.0f;
        tag_quality_t q1, q2;
        uint8_t out_mask = current_state; 
        
        if (reg_map_get_value(TAG_ID_LOCAL_RELAY_1, &out1_val, &q1) && (q1 == TAG_QUAL_GOOD)) {
            if (out1_val > 0.5f) out_mask |= (1 << 4); else out_mask &= ~(1 << 4);
        }
        if (reg_map_get_value(TAG_ID_LOCAL_RELAY_2, &out2_val, &q2) && (q2 == TAG_QUAL_GOOD)) {
            if (out2_val > 0.5f) out_mask |= (1 << 5); else out_mask &= ~(1 << 5);
        }
        if (out_mask != current_state) {
            bsp_i2c_write(0, PCF8574_ADDR, &out_mask, 1);
        }

        // ==========================================
        // 3. 轮询 ADS1115，并进行动态标定运算 (V4.1 核心威力)
        // ==========================================
        for (int ch = 0; ch < 4; ch++) {
            float voltage = 0.0f;
            uint16_t tag_id = TAG_ID_LOCAL_AIN_0 + ch;
            
            if (bsp_ads1115_read_single_ended(0, ADS1115_I2C_ADDR_GND, ch, ADS1115_PGA_6_144V, &voltage)) {
                float scale = 1.0f, offset = 0.0f;
                // 自动抓取并套用 JSON 里的量程公式，比如算出实际的压力或液位米数！
                get_local_tag_cfg(tag_id, NULL, &scale, &offset);
                float final_val = (voltage * scale) + offset;
                
                reg_map_update_value(tag_id, final_val);
            } else {
                reg_map_update_quality(tag_id, TAG_QUAL_SENSOR_ERR);
            }
        }

        tick_count++;
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void io_manager_init(void) {
    gpio_config_t btn_conf = {
        .pin_bit_mask = (1ULL << GPIO_BTN_RESET),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&btn_conf);

    gpio_config_t led_conf = {
        .pin_bit_mask = (1ULL << GPIO_LED_1) | (1ULL << GPIO_LED_2) | (1ULL << GPIO_LED_3),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&led_conf);

    // ========================================================
    // 终极重构：遍历本地字典，融合 JSON 动态配置后，统一挂载入 RTDB
    // ========================================================
    int dict_size = sizeof(s_local_io_dict) / sizeof(s_local_io_dict[0]);
    for (int i = 0; i < dict_size; i++) {
        const local_io_dict_t *tmpl = &s_local_io_dict[i];
        
        bool json_persist = false; // 默认所有本地测点不记忆
        
        // 尝试从 Web 组态 (JSON) 中抓取当前点位是否被赋予了“掉电记忆”属性
        get_local_tag_cfg(tmpl->tag_id, &json_persist, NULL, NULL);

        rtdb_tag_cfg_t cfg = { 
            .type = tmpl->type, 
            .writable = tmpl->writable, 
            .persist = json_persist, 
            .reverse_reg = 0xFFFF, 
            .slave_id = 0 
        };
        strncpy(cfg.name, tmpl->name, sizeof(cfg.name)-1);
        
        // 抹平差异，统统使用高级接口进行初始化注册！
        reg_map_add_tag_ext(tmpl->tag_id, &cfg);
    }

    xTaskCreate(io_poll_task, "io_poll", 4096, NULL, 4, NULL);
}