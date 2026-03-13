/**
 * @file io_manager.c
 * @brief 应用层：本地 IO、按键监控与指示灯控制
 * @note 整合了物理复位按键 (长按5秒) 与 AP 模式跑马灯特效，完全遵循 RTDB 虚拟化映射设计
 */

#include "io_manager.h"
#include "bsp_i2c.h"
#include "bsp_ads1115.h"
#include "bsp_wifi.h" // 获取 AP 模式状态
#include "register_map.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "driver/gpio.h"
#include "gateway_tags.h"
#include <unistd.h>

#define PCF8574_ADDR 0x20
static const char *TAG = "IO_MGR";

// 物理引脚定义 (根据实际硬件修改)
#define GPIO_BTN_RESET  0   // BOOT 按键 (内部上拉，按下为低电平)
#define GPIO_LED_1      10  // 指示灯 1
#define GPIO_LED_2      11  // 指示灯 2
#define GPIO_LED_3      12  // 指示灯 3

static void io_poll_task(void *arg) 
{
    uint8_t last_input = 0xFF;
    int btn_press_ms = 0;
    int tick_count = 0;
    
    while (1) {
        // ==========================================
        // 1. 物理按键与出厂重置监控 (长按 5 秒)
        // ==========================================
        if (gpio_get_level(GPIO_BTN_RESET) == 0) {
            btn_press_ms += 100;
            reg_map_update_value(TAG_ID_SYS_BTN_RESET, 1.0f); // 同步状态到 RTDB
            
            if (btn_press_ms >= 5000) {
                ESP_LOGW(TAG, "Factory Reset Triggered! Clearing Wi-Fi credentials...");
                unlink("/vfs/wifi.json"); // 利用 POSIX 标准接口删除配置文件
                
                // 物理指示灯疯狂闪烁 5 次，给工程师极其明确的“重置成功”视觉反馈
                for (int i = 0; i < 5; i++) {
                    gpio_set_level(GPIO_LED_1, 1); gpio_set_level(GPIO_LED_2, 1); gpio_set_level(GPIO_LED_3, 1);
                    vTaskDelay(pdMS_TO_TICKS(100));
                    gpio_set_level(GPIO_LED_1, 0); gpio_set_level(GPIO_LED_2, 0); gpio_set_level(GPIO_LED_3, 0);
                    vTaskDelay(pdMS_TO_TICKS(100));
                }
                esp_restart(); // 软重启系统，底层将因为找不到配置而自动弹射配网热点
            }
        } else {
            btn_press_ms = 0;
            reg_map_update_value(TAG_ID_SYS_BTN_RESET, 0.0f);
        }

        // ==========================================
        // 2. 指示灯状态生成器 (RTDB 虚拟写入层)
        // ==========================================
        if (bsp_wifi_is_ap_mode()) {
            // AP 配网模式下，每 200ms 移位一次，向 RTDB 注入跑马灯数据
            if (tick_count % 2 == 0) { 
                int step = (tick_count / 2) % 3;
                reg_map_update_value(TAG_ID_SYS_LED_1, step == 0 ? 1.0f : 0.0f);
                reg_map_update_value(TAG_ID_SYS_LED_2, step == 1 ? 1.0f : 0.0f);
                reg_map_update_value(TAG_ID_SYS_LED_3, step == 2 ? 1.0f : 0.0f);
            }
        } else {
            // 在线模式下：可以根据网关连通性点亮常亮灯
            // float is_online = bsp_wifi_is_connected() ? 1.0f : 0.0f;
            // reg_map_update_value(TAG_ID_SYS_LED_1, is_online);
        }

        // ==========================================
        // 3. 物理指示灯驱动 (纯粹从 RTDB 读取映射)
        // ==========================================
        float l1 = 0, l2 = 0, l3 = 0;
        reg_map_get_value(TAG_ID_SYS_LED_1, &l1, NULL);
        reg_map_get_value(TAG_ID_SYS_LED_2, &l2, NULL);
        reg_map_get_value(TAG_ID_SYS_LED_3, &l3, NULL);

        gpio_set_level(GPIO_LED_1, l1 > 0.5f);
        gpio_set_level(GPIO_LED_2, l2 > 0.5f);
        gpio_set_level(GPIO_LED_3, l3 > 0.5f);

        // ==========================================
        // 4. PCF8574 扩展 IO 同步逻辑
        // ==========================================
        uint8_t current_state = 0;
        esp_err_t ret = bsp_i2c_read(0, PCF8574_ADDR, &current_state, 1);
        
        if (ret == ESP_OK) {
            // 输入上报：如果有输入状态改变，更新给 RTDB
            if (current_state != last_input) {
                last_input = current_state;
                reg_map_update_value(TAG_ID_LOCAL_IO_INPUTS, (float)current_state);
            }
        }

        // 输出控制：读取 RTDB，写入到 I2C 扩展芯片
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
        // 5. 轮询本地 ADS1115 四通道模拟量，上报 RTDB
        // ==========================================
        for (int ch = 0; ch < 4; ch++) {
            float voltage = 0.0f;
            // 采用 6.144V 宽量程配置，适配工业 5V 传感场景
            if (bsp_ads1115_read_single_ended(0, ADS1115_I2C_ADDR_GND, ch, ADS1115_PGA_6_144V, &voltage)) {
                // 直接将读取到的电压值刷入 RTDB 中，业务端无需关心底层 I2C 时序
                reg_map_update_value(TAG_ID_LOCAL_AIN_0 + ch, voltage);
            } else {
                // 若读取失败，系统打上 Timeout / Sensor Error 质量戳
                reg_map_update_quality(TAG_ID_LOCAL_AIN_0 + ch, TAG_QUAL_SENSOR_ERR);
            }
        }

        tick_count++;
        vTaskDelay(pdMS_TO_TICKS(100)); // 100ms 刷新周期，保障响应与性能平衡
    }
}

void io_manager_init(void) {
    // 1. 初始化物理按键 GPIO (启用内部上拉，防止悬空误触)
    gpio_config_t btn_conf = {
        .pin_bit_mask = (1ULL << GPIO_BTN_RESET),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&btn_conf);

    // 2. 初始化物理指示灯 GPIO
    gpio_config_t led_conf = {
        .pin_bit_mask = (1ULL << GPIO_LED_1) | (1ULL << GPIO_LED_2) | (1ULL << GPIO_LED_3),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&led_conf);

    // 3. 向网关中枢注册系统级虚拟测点 (供外界查询或后续远程覆盖)
    // 基础IO掩码
    reg_map_add_tag(TAG_ID_LOCAL_IO_INPUTS, "PCF_Inputs", TAG_TYPE_INT32, true);
    
    // 按键与LED灯
    reg_map_add_tag(TAG_ID_SYS_BTN_RESET, "SYS_Btn_Reset", TAG_TYPE_BOOL, false);
    reg_map_add_tag(TAG_ID_SYS_LED_1, "SYS_LED_1", TAG_TYPE_BOOL, false);
    reg_map_add_tag(TAG_ID_SYS_LED_2, "SYS_LED_2", TAG_TYPE_BOOL, false);
    reg_map_add_tag(TAG_ID_SYS_LED_3, "SYS_LED_3", TAG_TYPE_BOOL, false);

    // 4. 启动 IO 监控核心任务
    xTaskCreate(io_poll_task, "io_poll", 4096, NULL, 4, NULL);
}