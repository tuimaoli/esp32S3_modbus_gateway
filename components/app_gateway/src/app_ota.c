/**
 * @file app_ota.c
 * @brief 应用层：系统 OTA 升级管理器实现
 */
#include "app_ota.h"
#include "esp_ota_ops.h"
#include "esp_log.h"

static const char *TAG = "APP_OTA";

// 内部状态维护
static esp_ota_handle_t s_update_handle = 0;
static const esp_partition_t *s_update_partition = NULL;
static bool s_is_running = false;

app_ota_err_t app_ota_begin(void) {
    if (s_is_running) {
        ESP_LOGW(TAG, "OTA already in progress, aborting previous session.");
        app_ota_abort();
    }

    // 获取下一个准备写入的可用 OTA 分区 (Ping-Pong 机制)
    s_update_partition = esp_ota_get_next_update_partition(NULL);
    if (s_update_partition == NULL) {
        ESP_LOGE(TAG, "Passive OTA partition not found");
        return APP_OTA_ERR_PARTITION;
    }

    ESP_LOGI(TAG, "Writing to partition subtype %d at offset 0x%lx",
             s_update_partition->subtype, s_update_partition->address);

    // 开始顺序写入模式
    esp_err_t err = esp_ota_begin(s_update_partition, OTA_WITH_SEQUENTIAL_WRITES, &s_update_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed (%s)", esp_err_to_name(err));
        return APP_OTA_ERR_BEGIN;
    }

    s_is_running = true;
    return APP_OTA_OK;
}

app_ota_err_t app_ota_write_chunk(const void *data, size_t length) {
    if (!s_is_running || s_update_handle == 0 || data == NULL || length == 0) {
        return APP_OTA_ERR_WRITE;
    }

    esp_err_t err = esp_ota_write(s_update_handle, data, length);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_write failed (%s)", esp_err_to_name(err));
        app_ota_abort();
        return APP_OTA_ERR_WRITE;
    }
    
    return APP_OTA_OK;
}

app_ota_err_t app_ota_end(void) {
    if (!s_is_running) return APP_OTA_ERR_VALIDATE;

    s_is_running = false;

    // 校验固件完整性 (验证 Magic Byte 和 MD5)
    esp_err_t err = esp_ota_end(s_update_handle);
    s_update_handle = 0; // 置空防二次操作
    
    if (err != ESP_OK) {
        if (err == ESP_ERR_OTA_VALIDATE_FAILED) {
            ESP_LOGE(TAG, "Image validation failed, image is corrupted");
        } else {
            ESP_LOGE(TAG, "esp_ota_end failed (%s)!", esp_err_to_name(err));
        }
        return APP_OTA_ERR_VALIDATE;
    }

    // 设置启动指针
    err = esp_ota_set_boot_partition(s_update_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed (%s)!", esp_err_to_name(err));
        return APP_OTA_ERR_BOOT_CFG;
    }

    ESP_LOGI(TAG, "OTA Success! Boot partition updated.");
    return APP_OTA_OK;
}

void app_ota_abort(void) {
    if (s_is_running && s_update_handle != 0) {
        esp_ota_abort(s_update_handle);
        s_update_handle = 0;
    }
    s_is_running = false;
    ESP_LOGW(TAG, "OTA Aborted.");
}