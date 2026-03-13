/**
 * @file app_mqtt.c
 * @brief MQTT 高级业务实现 (融合云端远程 OTA 与指令反解析)
 */
#include "app_mqtt.h"
#include "app_sntp.h"
#include "register_map.h"
#include "mqtt_client.h"
#include "esp_http_client.h"
#include "app_ota.h"         
#include "esp_log.h"
#include "cJSON.h"
#include "utils.h"           // 架构修正：引入全新封装的全局通用工具库
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

static const char __attribute__((unused)) *TAG = "APP_MQTT";
static esp_mqtt_client_handle_t g_mqtt_client = NULL;

static const sensor_device_t *g_sensors = NULL;
static int g_sensor_count = 0;

static QueueHandle_t g_mqtt_tx_queue = NULL;
#define MQTT_QUEUE_SIZE 50

extern float reg_map_read_value(uint16_t tag_id); 

void app_mqtt_enqueue_data(const char *sensor_name, const char *metric_name, float value) {
    if (!g_mqtt_tx_queue) return;
    mqtt_msg_t msg;
    strncpy(msg.sensor_name, sensor_name, sizeof(msg.sensor_name) - 1);
    strncpy(msg.metric_name, metric_name, sizeof(msg.metric_name) - 1);
    msg.value = value;
    
    xQueueSend(g_mqtt_tx_queue, &msg, 0); 
}

/* ============================================================
 * 异步拉取任务：云端触发的 HTTP 固件下载
 * ============================================================ */
static void mqtt_ota_download_task(void *arg) {
    char *url = (char *)arg;
    ESP_LOGW(TAG, "Starting Cloud-triggered OTA from: %s", url);

    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = 15000,
        .keep_alive_enable = true,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    
    if (esp_http_client_open(client, 0) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open HTTP connection to OTA server");
        goto ota_cleanup;
    }

    esp_http_client_fetch_headers(client);
    
    // 复用应用层封装好的 OTA 漏斗引擎
    if (app_ota_begin() != APP_OTA_OK) {
        goto ota_cleanup;
    }

    char buf[1024];
    int read_len;
    // 边下边写
    while ((read_len = esp_http_client_read(client, buf, sizeof(buf))) > 0) {
        if (app_ota_write_chunk(buf, read_len) != APP_OTA_OK) {
            ESP_LOGE(TAG, "OTA chunk write failed during download");
            goto ota_cleanup;
        }
        vTaskDelay(pdMS_TO_TICKS(1)); // 喂狗
    }

    // 校验 MD5 切换分区并重启
    if (app_ota_end() == APP_OTA_OK) {
        ESP_LOGI(TAG, "Cloud OTA Download complete! System Rebooting...");
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_restart();
    }

ota_cleanup:
    app_ota_abort(); // 出错回滚
    esp_http_client_cleanup(client);
    free(url);
    vTaskDelete(NULL);
}

/* ============================================================
 * 核心：MQTT 事件与云端指令反解析
 * ============================================================ */
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = event_data;
    const gateway_config_t *gw_cfg = config_manager_get_gw_cfg();

    if (event_id == MQTT_EVENT_CONNECTED) {
        ESP_LOGI(TAG, "MQTT Connected! Auto-subscribing...");
        for(int i = 0; i < gw_cfg->sub_topic_count; i++) {
            if (strlen(gw_cfg->mqtt_sub_topics[i]) > 1) {
                esp_mqtt_client_subscribe(g_mqtt_client, gw_cfg->mqtt_sub_topics[i], 0);
            }
        }
    } else if (event_id == MQTT_EVENT_DATA) {
        ESP_LOGI(TAG, "MQTT RX: Topic=%.*s", event->topic_len, event->topic);
        
        // 动态分配内存拷贝 payload，转成以 null 结尾的 C 字符串，供 cJSON 解析
        char *json_data = malloc(event->data_len + 1);
        if (json_data) {
            memcpy(json_data, event->data, event->data_len);
            json_data[event->data_len] = '\0';
            
            cJSON *root = cJSON_Parse(json_data);
            if (root) {
                cJSON *cmd = cJSON_GetObjectItem(root, "cmd");
                if (cmd && cmd->valuestring) {
                    // 指令 1：写本地变量
                    if (strcmp(cmd->valuestring, "write_tag") == 0) {
                        cJSON *tag_id = cJSON_GetObjectItem(root, "target_tag_id");
                        cJSON *val = cJSON_GetObjectItem(root, "value");
                        if (tag_id && val) {
                            reg_map_update_value((uint16_t)tag_id->valueint, (float)val->valuedouble);
                            ESP_LOGI(TAG, "Cloud CMD: Write Tag %d = %.2f", tag_id->valueint, val->valuedouble);
                        }
                    } 
                    // 指令 2：远程云端 OTA (例: {"cmd": "ota", "url": "http://192.168.1.100/fw.bin"})
                    else if (strcmp(cmd->valuestring, "ota") == 0) {
                        cJSON *url = cJSON_GetObjectItem(root, "url");
                        if (url && url->valuestring) {
                            char *dl_url = strdup(url->valuestring);
                            // 开启独立的后台下载线程，绝不阻塞 MQTT 心跳！
                            xTaskCreate(mqtt_ota_download_task, "mqtt_ota", 8192, dl_url, 5, NULL);
                        }
                    }
                    // 新增指令 3：远程拉取当前配置文件
                    else if (strcmp(cmd->valuestring, "read_config") == 0) {
                        char *cfg_str = config_manager_get_json();
                        if (cfg_str) {
                            char topic_buf[128];
                            snprintf(topic_buf, sizeof(topic_buf), "/gw/%s/config_resp", gw_cfg->device_id);
                            esp_mqtt_client_publish(g_mqtt_client, topic_buf, cfg_str, 0, 0, 0);
                            free(cfg_str);
                        }
                    }
                    // 新增指令 4：远程下发并覆盖配置文件
                    else if (strcmp(cmd->valuestring, "write_config") == 0) {
                        cJSON *new_cfg = cJSON_GetObjectItem(root, "data");
                        if (new_cfg) {
                            char *new_cfg_str = cJSON_PrintUnformatted(new_cfg);
                            char err_msg[128] = {0};
                            
                            // 调用 Utils 工具进行严格防呆校验
                            if (utils_validate_gateway_config(new_cfg_str, err_msg, sizeof(err_msg))) {
                                ESP_LOGI(TAG, "Remote Config valid, saving and rebooting...");
                                config_manager_save_json(new_cfg_str);
                                free(new_cfg_str);
                                vTaskDelay(pdMS_TO_TICKS(1000));
                                esp_restart();
                            } else {
                                ESP_LOGE(TAG, "Remote Config invalid: %s", err_msg);
                                free(new_cfg_str);
                            }
                        }
                    }
                }
                cJSON_Delete(root);
            }
            free(json_data);
        }
    }
}

static void mqtt_tx_task(void *arg) {
    mqtt_msg_t msg;
    const gateway_config_t *gw_cfg = config_manager_get_gw_cfg();
    char time_stamp[64];
    char topic_buf[128];

    while (1) {
        if (xQueueReceive(g_mqtt_tx_queue, &msg, portMAX_DELAY) == pdTRUE) {
            if (!g_mqtt_client) continue;

            app_sntp_get_iso8601(time_stamp); 
            
            cJSON *root = cJSON_CreateObject();
            cJSON_AddStringToObject(root, "device_id", gw_cfg->device_id);
            cJSON_AddStringToObject(root, "timestamp", time_stamp);
            cJSON_AddStringToObject(root, "sensor_name", msg.sensor_name);
            cJSON *metrics = cJSON_AddObjectToObject(root, "metrics");
            cJSON_AddNumberToObject(metrics, msg.metric_name, msg.value);

            snprintf(topic_buf, sizeof(topic_buf), "/gw/%s/data/%s", gw_cfg->device_id, msg.sensor_name);
            char *json_str = cJSON_PrintUnformatted(root);
            
            esp_mqtt_client_publish(g_mqtt_client, topic_buf, json_str, 0, 0, 0);
            
            free(json_str);
            cJSON_Delete(root);
        }
    }
}

static void mqtt_periodic_full_upload_task(void *arg) {
    const gateway_config_t *gw_cfg = config_manager_get_gw_cfg();
    uint32_t interval = gw_cfg->upload_interval_ms > 1000 ? gw_cfg->upload_interval_ms : 10000;
    char time_stamp[64];
    char topic_buf[128];

    vTaskDelay(pdMS_TO_TICKS(5000));

    while (1) {
        if (g_mqtt_client && g_sensors && g_sensor_count > 0) {
            app_sntp_get_iso8601(time_stamp);
            
            cJSON *root = cJSON_CreateObject();
            cJSON_AddStringToObject(root, "device_id", gw_cfg->device_id);
            cJSON_AddStringToObject(root, "timestamp", time_stamp);
            cJSON *data_all = cJSON_AddObjectToObject(root, "data");

            for (int i = 0; i < g_sensor_count; i++) {
                const sensor_device_t *dev = &g_sensors[i];
                cJSON *dev_node = cJSON_AddObjectToObject(data_all, dev->name);
                
                float status_val = 0;
                reg_map_get_value(dev->status_tag_id, &status_val, NULL);
                cJSON_AddNumberToObject(dev_node, "online", status_val);
                
                for (int j = 0; j < dev->rule_count; j++) {
                    float metric_val = 0;
                    reg_map_get_value(dev->rules[j].target_tag_id, &metric_val, NULL);
                    cJSON_AddNumberToObject(dev_node, dev->rules[j].name, metric_val);
                }
            }

            snprintf(topic_buf, sizeof(topic_buf), "/gw/%s/data/all", gw_cfg->device_id);
            char *json_str = cJSON_PrintUnformatted(root);
            esp_mqtt_client_publish(g_mqtt_client, topic_buf, json_str, 0, 0, 0);
            
            free(json_str);
            cJSON_Delete(root);
        }
        vTaskDelay(pdMS_TO_TICKS(interval));
    }
}

void app_mqtt_start(const sensor_device_t *sensors, int sensor_count) {
    g_sensors = sensors;
    g_sensor_count = sensor_count;
    
    if (g_mqtt_tx_queue == NULL) {
        g_mqtt_tx_queue = xQueueCreate(MQTT_QUEUE_SIZE, sizeof(mqtt_msg_t));
    }
    
    const gateway_config_t *gw_cfg = config_manager_get_gw_cfg();
    if (strlen(gw_cfg->mqtt_broker) < 6) return;

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = gw_cfg->mqtt_broker,
        .credentials.username = gw_cfg->mqtt_user,
        .credentials.authentication.password = gw_cfg->mqtt_pass,
    };

    g_mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    
    esp_mqtt_client_register_event(g_mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(g_mqtt_client);

    xTaskCreate(mqtt_tx_task, "mqtt_tx_worker", 4096, NULL, 5, NULL);
    xTaskCreate(mqtt_periodic_full_upload_task, "mqtt_full_snap", 6144, NULL, 4, NULL);
}