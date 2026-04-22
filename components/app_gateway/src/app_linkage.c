/**
 * @file app_linkage.c
 * @brief 应用层：边缘联动逻辑引擎实现
 * @note V4.0 基于递归下降解析器，支持复杂字符串表达式与数学函数联动
 */
#include "app_linkage.h"
#include "config_manager.h"
#include "register_map.h"
#include "utils_expr.h"    // 引入强大的表达式解析引擎
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "LINKAGE";

/**
 * @brief 提供给表达式解析引擎的钩子函数，用于在解析到 TAG(xxx) 时获取 RTDB 实时数据
 */
static bool rtdb_var_fetch_cb(uint16_t tag_id, float *out_val) {
    tag_quality_t quality = TAG_QUAL_INIT;
    if (reg_map_get_value(tag_id, out_val, &quality)) {
        // 安全墙设计：只有当传感器完全在线且数据良好时，这个值才被允许带入计算式
        // 防止传感器断电后，拿最后一次的旧值错误地触发联动逻辑！
        if (quality == TAG_QUAL_GOOD) {
            return true;
        }
    }
    return false; // 如果测点不存在，或传感器超时/报错，直接切断本次逻辑运算
}

static void linkage_task(void *arg) {
    ESP_LOGI(TAG, "Edge Linkage Engine Started.");
    
    while (1) {
        int rule_count = 0;
        const linkage_rule_t *rules = config_manager_get_linkages(&rule_count);
        
        if (rules != NULL && rule_count > 0) {
            for (int i = 0; i < rule_count; i++) {
                linkage_rule_t *rule = (linkage_rule_t *)&rules[i]; 
                
                if (!rule->enable || rule->condition[0] == '\0') continue;

                float eval_result = 0.0f;
                
                // 核心威力：直接将表达式扔给引擎计算
                bool eval_success = utils_expr_eval(rule->condition, &eval_result, rtdb_var_fetch_cb);
                
                if (eval_success) {
                    bool condition_met = (eval_result > 0.5f); // >0.5f 视为逻辑真

                    // 边沿触发检测 (防止动作指令被疯狂重复下发导致总线拥堵)
                    if (condition_met && !rule->_last_state) {
                        ESP_LOGW(TAG, "Linkage [%s] Triggered! Expression met -> Writing Action(%d):%.2f", 
                                 rule->name, rule->action_tag_id, rule->action_value);
                        
                        reg_map_write_value(rule->action_tag_id, rule->action_value);
                    }
                    
                    // 状态跟踪更新
                    rule->_last_state = condition_met;
                }
            }
        }
        
        // 500 毫秒扫描周期
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

void app_linkage_start(void) {
    xTaskCreate(linkage_task, "edge_linkage", 5120, NULL, 4, NULL); // 解析器涉及一定栈深度，给 5KB
}