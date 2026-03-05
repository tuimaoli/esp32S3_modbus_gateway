/**
 * @file bsp_fs.c
 * @brief BSP层：LittleFS 驱动实现
 */
#include "bsp_fs.h"
#include "esp_littlefs.h"
#include "esp_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "BSP_FS";
#define BASE_PATH "/vfs"

esp_err_t bsp_fs_init(void) {
    esp_vfs_littlefs_conf_t conf = {
        .base_path = BASE_PATH,
        .partition_label = "storage",
        .format_if_mount_failed = true, // 首次运行自动格式化
        .dont_mount = false,
    };

    esp_err_t ret = esp_vfs_littlefs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount LittleFS (%s)", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "LittleFS mounted at %s", BASE_PATH);
    return ESP_OK;
}

char* bsp_fs_read_file_to_str(const char* path) {
    FILE *f = fopen(path, "r");
    if (f == NULL) {
        ESP_LOGW(TAG, "File not found: %s", path);
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *string = malloc(fsize + 1);
    if (!string) {
        fclose(f);
        return NULL;
    }

    fread(string, 1, fsize, f);
    string[fsize] = '\0';
    fclose(f);
    return string;
}

esp_err_t bsp_fs_write_str_to_file(const char* path, const char* content) {
    FILE *f = fopen(path, "w");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file for writing: %s", path);
        return ESP_FAIL;
    }
    fprintf(f, "%s", content);
    fclose(f);
    return ESP_OK;
}