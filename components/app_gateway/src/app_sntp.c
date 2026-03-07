/**
 * @file app_sntp.c
 * @brief 自动网络校时与时间戳生成
 */
#include "app_sntp.h"
#include "esp_sntp.h"
#include "esp_log.h"
#include <time.h>
#include <sys/time.h>

static const char *TAG = "APP_SNTP";

// 校时回调函数
static void time_sync_notification_cb(struct timeval *tv) {
    ESP_LOGI(TAG, "Notification of a time synchronization event");
    char time_str[64];
    app_sntp_get_iso8601(time_str);
    ESP_LOGI(TAG, "Current Network Time: %s", time_str);
}

void app_sntp_init(void) {
    ESP_LOGI(TAG, "Initializing SNTP...");
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    
    // 设置公共 NTP 服务器 (推荐阿里云、腾讯云或国家授时中心)
    esp_sntp_setservername(0, "ntp.aliyun.com");
    esp_sntp_setservername(1, "time.pool.aliyun.com");
    esp_sntp_setservername(2, "cn.pool.ntp.org");
    
    sntp_set_time_sync_notification_cb(time_sync_notification_cb);
    esp_sntp_init();

    // 极其关键：设置时区为中国标准时间 (CST-8)
    setenv("TZ", "CST-8", 1);
    tzset();
}

void app_sntp_get_iso8601(char *out_str) {
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    
    // 如果年份小于 2020，说明 NTP 还没同步成功，打上警告标记
    if (timeinfo.tm_year < (2020 - 1900)) {
        sprintf(out_str, "1970-01-01T00:00:00Z_UNSYNCED");
    } else {
        // 生成标准工业时间戳格式：2026-03-05T15:30:00+08:00
        strftime(out_str, 32, "%Y-%m-%dT%H:%M:%S+08:00", &timeinfo);
    }
}