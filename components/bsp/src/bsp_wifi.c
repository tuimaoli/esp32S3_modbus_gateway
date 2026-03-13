/**
 * @file bsp_wifi.c
 * @brief BSP层：Wi-Fi 驱动实现 (带 SoftAP 救生圈与 Smart Handoff IP 移交机制)
 * @note 引入 FreeRTOS EventGroup，实现在热点不断线的情况下，异步获取新路由 IP 并回传
 */
#include "bsp_wifi.h"
#include "bsp_fs.h"
#include "cJSON.h"
#include "esp_wifi.h"
#include "esp_mac.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "mdns.h"
#include "lwip/sockets.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h" // 新增：用于阻塞等待 IP 分配
#include <string.h>

static const char *TAG = "BSP_WIFI";

static bool g_wifi_connected = false;
static bool g_ap_mode_active = false;
static int g_retry_count = 0;

// 新增：用于保存上层注入的 device_id
static char g_device_id[32] = {0};

static SemaphoreHandle_t g_rescue_sem = NULL;
static TaskHandle_t g_dns_task_handle = NULL;

// 核心解耦：网络移交事件组与全局网卡句柄
static EventGroupHandle_t g_wifi_evg = NULL;
#define WIFI_EVT_GOT_IP BIT0
#define WIFI_EVT_FAIL   BIT1

static esp_netif_t *g_sta_netif = NULL;

/* ============================================================
 * 内部功能：加载持久化的 Wi-Fi 凭证
 * ============================================================ */
static bool load_wifi_credentials_from_vfs(bsp_wifi_config_t *out_cfg) {
    char *json_str = bsp_fs_read_file_to_str("/vfs/wifi.json");
    if (!json_str) return false;

    cJSON *root = cJSON_Parse(json_str);
    free(json_str);
    if (!root) return false;

    cJSON *ssid_node = cJSON_GetObjectItem(root, "ssid");
    cJSON *pass_node = cJSON_GetObjectItem(root, "password");
    if (ssid_node && pass_node && strlen(ssid_node->valuestring) > 0) {
        strncpy(out_cfg->ssid, ssid_node->valuestring, sizeof(out_cfg->ssid) - 1);
        strncpy(out_cfg->password, pass_node->valuestring, sizeof(out_cfg->password) - 1);
        cJSON_Delete(root);
        return true;
    }
    cJSON_Delete(root);
    return false;
}

/* ============================================================
 * 内部功能：极简 DNS 服务器 (Captive Portal)
 * ============================================================ */
static void captive_dns_task(void *pvParameters) {
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    struct sockaddr_in server_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(53),
        .sin_addr.s_addr = htonl(INADDR_ANY)
    };
    bind(sock, (struct sockaddr *)&server_addr, sizeof(server_addr));
    
    // ⚡ 架构升级：设置 Socket 接收超时，防止死锁，允许任务检查生命周期标志位
    struct timeval tv = { .tv_sec = 2, .tv_usec = 0 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    ESP_LOGI(TAG, "Captive Portal DNS Server started on port 53");

    uint8_t rx_buf[128];
    // 只要热点模式激活，就一直循环；热点关闭，立刻退出循环
    while (g_ap_mode_active) {
        struct sockaddr_in client_addr;
        socklen_t len = sizeof(client_addr);
        int rx_len = recvfrom(sock, rx_buf, sizeof(rx_buf), 0, (struct sockaddr *)&client_addr, &len);
        
        // 正常收到 DNS 请求包
        if (rx_len > 12) {
            rx_buf[2] = 0x81; rx_buf[3] = 0x80;
            rx_buf[6] = 0x00; rx_buf[7] = 0x01;
            rx_buf[8] = 0x00; rx_buf[9] = 0x00;
            rx_buf[10]= 0x00; rx_buf[11]= 0x00;
            
            uint8_t ans_rr[] = {0xC0, 0x0C, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x01, 0x2C, 0x00, 0x04, 192, 168, 4, 1};
            if (rx_len + sizeof(ans_rr) <= sizeof(rx_buf)) {
                memcpy(rx_buf + rx_len, ans_rr, sizeof(ans_rr));
                sendto(sock, rx_buf, rx_len + sizeof(ans_rr), 0, (struct sockaddr *)&client_addr, len);
            }
        }
    }
    
    // 自杀：释放网络套接字 -> 清除句柄 -> 删除任务自身，完美回收 3KB 内存
    ESP_LOGI(TAG, "Captive Portal disabled. Reclaiming DNS task resources.");
    close(sock);
    g_dns_task_handle = NULL;
    vTaskDelete(NULL);
}

/* ============================================================
 * 外部接口：注入设备 ID
 * ============================================================ */
void bsp_wifi_set_device_id(const char* device_id) {
    if (device_id) {
        strncpy(g_device_id, device_id, sizeof(g_device_id) - 1);
    }
}

/* ============================================================
 * 内部功能：启动 SoftAP 救生圈模式
 * ============================================================ */
static void start_softap_rescue_mode(void) {
    if (g_ap_mode_active) return;
    g_ap_mode_active = true;

    wifi_config_t ap_config = {
        .ap = {
            .ssid_len = 0,
            .channel = 1,
            .password = "", 
            .max_connection = 4,
            .authmode = WIFI_AUTH_OPEN
        },
    };

    // 架构升级：优先使用注入的 device_id 作为热点名，若空则回退到 MAC 地址兜底
    if (strlen(g_device_id) > 0) {
        snprintf((char *)ap_config.ap.ssid, sizeof(ap_config.ap.ssid), "%s", g_device_id);
    } else {
        uint8_t mac[6];
        esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
        snprintf((char *)ap_config.ap.ssid, sizeof(ap_config.ap.ssid), "GW_ESP32_%02X%02X", mac[4], mac[5]);
    }

    ESP_LOGW(TAG, "Entering Rescue Mode! Broadcasting SoftAP: %s", ap_config.ap.ssid);
    
    esp_wifi_set_mode(WIFI_MODE_APSTA);
    esp_wifi_set_config(WIFI_IF_AP, &ap_config);
    
    if (g_dns_task_handle == NULL) {
        xTaskCreate(captive_dns_task, "captive_dns", 3072, NULL, 5, &g_dns_task_handle);
    }
}

static void wifi_rescue_monitor_task(void *arg) {
    vTaskDelay(pdMS_TO_TICKS(10000));
    if (!g_wifi_connected && !g_ap_mode_active) {
        start_softap_rescue_mode();
    }
    while (1) {
        if (xSemaphoreTake(g_rescue_sem, pdMS_TO_TICKS(15000)) == pdTRUE) {
            if (!g_ap_mode_active) {
                start_softap_rescue_mode();
            }
        } else {
            if (!g_wifi_connected && g_retry_count > 5) {
                esp_wifi_connect();
            }
        }
    }
}

/* ============================================================
 * 事件处理机制
 * ============================================================ */
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        g_wifi_connected = false;
        g_retry_count++;
        ESP_LOGW(TAG, "Wi-Fi disconnected. Retry: %d", g_retry_count);
        
        if (g_retry_count <= 5) {
            esp_wifi_connect(); 
        } else {
            // 失败抛出事件，供 Smart Handoff 接口捕获
            if (g_wifi_evg) xEventGroupSetBits(g_wifi_evg, WIFI_EVT_FAIL);
            if (g_rescue_sem) xSemaphoreGive(g_rescue_sem);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Wi-Fi got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        g_wifi_connected = true;
        g_retry_count = 0;
        
        // 成功获取 IP 抛出事件
        if (g_wifi_evg) xEventGroupSetBits(g_wifi_evg, WIFI_EVT_GOT_IP);
        
        // 【注意：此处移除了立刻关闭 AP 的代码！为了保证给手机发送完最终 IP 的 HTTP 响应】
    }
}

static void start_mdns_service(void) {
    mdns_init();
    mdns_hostname_set("gw-esp32"); 
    mdns_instance_name_set("IoT Edge Gateway");
    mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
}

/* ============================================================
 * 外部接口：Smart Handoff (供 WebServer 调用获取新 IP)
 * ============================================================ */
bool bsp_wifi_try_connect_and_get_ip(const char* ssid, const char* pass, char* out_ip, uint32_t timeout_ms) {
    wifi_config_t wifi_config = { .sta = { .pmf_cfg = { .capable = true, .required = false } } };
    strncpy((char *)wifi_config.sta.ssid, ssid, 32);
    strncpy((char *)wifi_config.sta.password, pass, 64);

    // 强行应用新配置
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    
    // 清除历史标志位
    xEventGroupClearBits(g_wifi_evg, WIFI_EVT_GOT_IP | WIFI_EVT_FAIL);
    g_retry_count = 0;
    
    // 重新连接 (此时手机连接的 SoftAP 不会断开！)
    esp_wifi_disconnect();
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_wifi_connect();

    ESP_LOGI(TAG, "Smart Handoff: Waiting for DHCP IP on new network...");
    
    // 阻塞等待连接结果
    EventBits_t bits = xEventGroupWaitBits(g_wifi_evg, WIFI_EVT_GOT_IP | WIFI_EVT_FAIL, 
                                           pdFALSE, pdFALSE, pdMS_TO_TICKS(timeout_ms));

    if (bits & WIFI_EVT_GOT_IP) {
        esp_netif_ip_info_t ip_info;
        esp_netif_get_ip_info(g_sta_netif, &ip_info);
        sprintf(out_ip, IPSTR, IP2STR(&ip_info.ip));
        return true;
    }
    return false;
}

/* ============================================================
 * 外部接口：初始化
 * ============================================================ */
esp_err_t bsp_wifi_init(const bsp_wifi_config_t *default_config) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    esp_netif_create_default_wifi_ap();
    g_sta_netif = esp_netif_create_default_wifi_sta();
    esp_netif_set_hostname(g_sta_netif, "gw-esp32");

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    bsp_wifi_config_t final_cfg = {0};
    bool has_saved_wifi = load_wifi_credentials_from_vfs(&final_cfg);
    if (!has_saved_wifi && default_config) {
        memcpy(&final_cfg, default_config, sizeof(bsp_wifi_config_t));
    }

    wifi_config_t wifi_config = {
        .sta = { .pmf_cfg = { .capable = true, .required = false }, },
    };
    strncpy((char *)wifi_config.sta.ssid, final_cfg.ssid, sizeof(wifi_config.sta.ssid));
    strncpy((char *)wifi_config.sta.password, final_cfg.password, sizeof(wifi_config.sta.password));

    g_wifi_evg = xEventGroupCreate();
    g_rescue_sem = xSemaphoreCreateBinary();
    xTaskCreate(wifi_rescue_monitor_task, "wifi_rescue", 4096, NULL, 3, NULL);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    start_mdns_service();

    if (strlen((char*)wifi_config.sta.ssid) == 0) {
        xSemaphoreGive(g_rescue_sem);
    }

    return ESP_OK;
}

bool bsp_wifi_is_connected(void) {
    return g_wifi_connected;
}

bool bsp_wifi_is_ap_mode(void) {
    return g_ap_mode_active;
}