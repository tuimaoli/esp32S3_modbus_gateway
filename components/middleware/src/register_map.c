/**
 * @file register_map.c
 * @brief 中间件层：微型实时数据库 (RTDB) V4.0 实现
 * @note 包含线程安全无锁读取、NVS 掉电记忆防磨损与反向控制路由
 */
#include "register_map.h"
#include "protocol_engine.h" // 架构依赖：引入协议引擎头文件，以调用 TX 队列
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static const char *TAG = "RTDB";

typedef struct rtdb_node {
    uint16_t        tag_id;
    char            name[32];
    tag_type_t      type;
    float           value;
    tag_quality_t   quality;
    uint32_t        timestamp;
    
    // V4.0 高级控制属性
    bool            writable;
    bool            persist;
    uint16_t        reverse_reg;
    uint8_t         slave_id;

    struct rtdb_node *next;
} rtdb_node_t;

static rtdb_node_t *g_tag_list_head = NULL;
static SemaphoreHandle_t g_rtdb_mutex = NULL;
static nvs_handle_t g_nvs_handle;

// 内部工具：通过 float 位级强转，方便 NVS 存取 U32
static inline uint32_t float_to_u32(float f) {
    uint32_t u; memcpy(&u, &f, sizeof(float)); return u;
}
static inline float u32_to_float(uint32_t u) {
    float f; memcpy(&f, &u, sizeof(uint32_t)); return f;
}

void reg_map_init(void) {
    if (g_rtdb_mutex == NULL) {
        g_rtdb_mutex = xSemaphoreCreateMutex();
    }
    
    // 初始化 NVS 存储，用于掉电记忆持久化测点
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    
    err = nvs_open("rtdb_store", NVS_READWRITE, &g_nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error opening NVS handle!");
    } else {
        ESP_LOGI(TAG, "RTDB Core and NVS Persistence Engine Initialized.");
    }
}

void reg_map_add_tag_ext(uint16_t tag_id, const rtdb_tag_cfg_t *cfg) {
    if (!g_rtdb_mutex) return;
    xSemaphoreTake(g_rtdb_mutex, portMAX_DELAY);

    // 防重名判断
    rtdb_node_t *curr = g_tag_list_head;
    while (curr != NULL) {
        if (curr->tag_id == tag_id) {
            xSemaphoreGive(g_rtdb_mutex);
            return; 
        }
        curr = curr->next;
    }

    rtdb_node_t *new_node = (rtdb_node_t *)malloc(sizeof(rtdb_node_t));
    if (new_node) {
        new_node->tag_id = tag_id;
        strncpy(new_node->name, cfg->name, sizeof(new_node->name) - 1);
        new_node->type = cfg->type;
        new_node->value = 0.0f;
        new_node->quality = TAG_QUAL_INIT;
        new_node->timestamp = xTaskGetTickCount();
        new_node->writable = cfg->writable;
        new_node->persist = cfg->persist;
        new_node->reverse_reg = cfg->reverse_reg;
        new_node->slave_id = cfg->slave_id;
        new_node->next = g_tag_list_head;
        g_tag_list_head = new_node;

        // 【掉电记忆恢复】：如果配置了记忆，尝试从 NVS 拉取历史最后一次的值
        if (cfg->persist) {
            char nvs_key[16];
            snprintf(nvs_key, sizeof(nvs_key), "t_%d", tag_id);
            uint32_t stored_val_u32 = 0;
            if (nvs_get_u32(g_nvs_handle, nvs_key, &stored_val_u32) == ESP_OK) {
                new_node->value = u32_to_float(stored_val_u32);
                ESP_LOGI(TAG, "Tag [%d] restored from NVS: %.2f", tag_id, new_node->value);
            }
        }
    }
    xSemaphoreGive(g_rtdb_mutex);
}

void reg_map_add_tag(uint16_t tag_id, const char *name, tag_type_t type, bool read_only) {
    rtdb_tag_cfg_t cfg = {
        .type = type,
        .writable = !read_only,
        .persist = false, 
        .reverse_reg = 0xFFFF,
        .slave_id = 0
    };
    strncpy(cfg.name, name, sizeof(cfg.name) - 1);
    reg_map_add_tag_ext(tag_id, &cfg);
}

void reg_map_update_value(uint16_t tag_id, float value) {
    if (!g_rtdb_mutex) return;
    xSemaphoreTake(g_rtdb_mutex, portMAX_DELAY);

    rtdb_node_t *curr = g_tag_list_head;
    while (curr != NULL) {
        if (curr->tag_id == tag_id) {
            // 【Flash 磨损保护】：仅在值发生实际变化时才触发擦写
            bool is_changed = (curr->value != value);
            curr->value = value;
            curr->quality = TAG_QUAL_GOOD; // 更新值证明总线通讯正常
            curr->timestamp = xTaskGetTickCount();
            
            if (is_changed && curr->persist) {
                char nvs_key[16];
                snprintf(nvs_key, sizeof(nvs_key), "t_%d", tag_id);
                nvs_set_u32(g_nvs_handle, nvs_key, float_to_u32(value));
                nvs_commit(g_nvs_handle);
            }
            break;
        }
        curr = curr->next;
    }
    xSemaphoreGive(g_rtdb_mutex);
}

void reg_map_update_quality(uint16_t tag_id, tag_quality_t quality) {
    if (!g_rtdb_mutex) return;
    xSemaphoreTake(g_rtdb_mutex, portMAX_DELAY);

    rtdb_node_t *curr = g_tag_list_head;
    while (curr != NULL) {
        if (curr->tag_id == tag_id) {
            curr->quality = quality;
            curr->timestamp = xTaskGetTickCount();
            break;
        }
        curr = curr->next;
    }
    xSemaphoreGive(g_rtdb_mutex);
}

bool reg_map_get_value(uint16_t tag_id, float *out_value, tag_quality_t *out_quality) {
    if (!g_rtdb_mutex) return false;
    xSemaphoreTake(g_rtdb_mutex, portMAX_DELAY);

    bool found = false;
    rtdb_node_t *curr = g_tag_list_head;
    while (curr != NULL) {
        if (curr->tag_id == tag_id) {
            if (out_value) *out_value = curr->value;
            if (out_quality) *out_quality = curr->quality;
            found = true;
            break;
        }
        curr = curr->next;
    }

    xSemaphoreGive(g_rtdb_mutex);
    return found;
}

bool reg_map_write_value(uint16_t tag_id, float new_value) {
    if (!g_rtdb_mutex) return false;
    xSemaphoreTake(g_rtdb_mutex, portMAX_DELAY);

    bool success = false;
    rtdb_node_t *curr = g_tag_list_head;
    while (curr != NULL) {
        if (curr->tag_id == tag_id) {
            if (curr->writable) {
                // 北向写入视同为合法控制变更，更新本地映射并记忆
                bool is_changed = (curr->value != new_value);
                curr->value = new_value;
                curr->timestamp = xTaskGetTickCount();
                
                // 1. NVS 掉电记忆保护
                if (is_changed && curr->persist) {
                    char nvs_key[16];
                    snprintf(nvs_key, sizeof(nvs_key), "t_%d", tag_id);
                    nvs_set_u32(g_nvs_handle, nvs_key, float_to_u32(new_value));
                    nvs_commit(g_nvs_handle);
                }
                
                // 2. ⚡【核心联动】：向底层协议引擎发射反向控制指令！
                if (curr->reverse_reg != 0xFFFF) {
                    ESP_LOGI(TAG, "Triggering Reverse TX: Slave %d, Reg %d, Val %.2f", 
                             curr->slave_id, curr->reverse_reg, new_value);
                    protocol_engine_push_tx_queue(curr->slave_id, curr->reverse_reg, new_value);
                }
                
                success = true;
            } else {
                ESP_LOGW(TAG, "Write blocked: Tag %d is read-only", tag_id);
            }
            break;
        }
        curr = curr->next;
    }

    xSemaphoreGive(g_rtdb_mutex);
    return success;
}