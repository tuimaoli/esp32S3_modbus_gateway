/**
 * @file app_mqtt.c
 * @brief MQTT 高级业务实现 (带 TX 消息队列、JSON 序列化与 SNTP 时间戳)
 */
#include "app_mqtt.h"
#include "app_sntp.h"
#include "register_map.h"
#include "mqtt_client.h"
#include "esp_log.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "transport_wiznet.h"

static const char __attribute__((unused)) *TAG = "APP_MQTT";
static esp_mqtt_client_handle_t g_mqtt_client = NULL;

// 传感器动态配置指针 (用于定时全量上报)
static const sensor_device_t *g_sensors = NULL;
static int g_sensor_count = 0;

// 核心解耦：MQTT 异步发送队列 (深度 50 足矣)
static QueueHandle_t g_mqtt_tx_queue = NULL;
#define MQTT_QUEUE_SIZE 50

extern float reg_map_read_value(uint16_t tag_id); // 依赖外部 RTDB 读取接口

/* ============================================================
 * 供采集任务调用的 API：压入发送队列 (极速非阻塞)
 * ============================================================ */
void app_mqtt_enqueue_data(const char *sensor_name, const char *metric_name, float value) {
    if (!g_mqtt_tx_queue) return;
    mqtt_msg_t msg;
    strncpy(msg.sensor_name, sensor_name, sizeof(msg.sensor_name) - 1);
    strncpy(msg.metric_name, metric_name, sizeof(msg.metric_name) - 1);
    msg.value = value;
    
    // 采用 0 等待时间。如果云端卡顿导致队列满了，丢弃旧数据，绝不阻塞轮询任务
    xQueueSend(g_mqtt_tx_queue, &msg, 0); 
}

/* ============================================================
 * MQTT 底层网络事件回调
 * ============================================================ */
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = event_data;
    const gateway_config_t *gw_cfg = config_manager_get_gw_cfg();

    if (event_id == MQTT_EVENT_CONNECTED) {
        ESP_LOGI(TAG, "MQTT Connected! Auto-subscribing to configured topics...");
        for(int i = 0; i < gw_cfg->sub_topic_count; i++) {
            if (strlen(gw_cfg->mqtt_sub_topics[i]) > 1) {
                esp_mqtt_client_subscribe(g_mqtt_client, gw_cfg->mqtt_sub_topics[i], 0);
            }
        }
    } else if (event_id == MQTT_EVENT_DISCONNECTED) {
        ESP_LOGW(TAG, "MQTT Disconnected. Waiting for auto-reconnect...");
    } else if (event_id == MQTT_EVENT_DATA) {
        ESP_LOGI(TAG, "MQTT RX: Topic=%.*s, Data=%.*s", event->topic_len, event->topic, event->data_len, event->data);
        // 此处可接入云端下发的反向控制逻辑
    }
}

/* ============================================================
 * 核心任务：后台异步发送引擎 (消费队列，生成 JSON，绝不卡死系统)
 * ============================================================ */
static void mqtt_tx_task(void *arg) {
    mqtt_msg_t msg;
    const gateway_config_t *gw_cfg = config_manager_get_gw_cfg();
    char time_stamp[64];
    char topic_buf[128];

    while (1) {
        // 阻塞等待队列中的数据，有数据立刻唤醒
        if (xQueueReceive(g_mqtt_tx_queue, &msg, portMAX_DELAY) == pdTRUE) {
            if (!g_mqtt_client) continue;

            app_sntp_get_iso8601(time_stamp); // 获取当时精准时间
            
            // 构建增量/单点上报的 JSON
            cJSON *root = cJSON_CreateObject();
            cJSON_AddStringToObject(root, "device_id", gw_cfg->device_id);
            cJSON_AddStringToObject(root, "timestamp", time_stamp);
            cJSON_AddStringToObject(root, "sensor_name", msg.sensor_name);
            cJSON *metrics = cJSON_AddObjectToObject(root, "metrics");
            cJSON_AddNumberToObject(metrics, msg.metric_name, msg.value);

            // 主题按传感器隔离：/gw/{gw_id}/data/{sensor_name}
            snprintf(topic_buf, sizeof(topic_buf), "/gw/%s/data/%s", gw_cfg->device_id, msg.sensor_name);
            char *json_str = cJSON_PrintUnformatted(root);
            
            // QOS=0: 发后即忘，最高吞吐量
            esp_mqtt_client_publish(g_mqtt_client, topic_buf, json_str, 0, 0, 0);
            
            free(json_str);
            cJSON_Delete(root);
        }
    }
}

/* ============================================================
 * 辅助任务：定时全量快照上报 (针对需要一次性获取所有状态的平台)
 * ============================================================ */
static void mqtt_periodic_full_upload_task(void *arg) {
    const gateway_config_t *gw_cfg = config_manager_get_gw_cfg();
    uint32_t interval = gw_cfg->upload_interval_ms > 1000 ? gw_cfg->upload_interval_ms : 10000;
    char time_stamp[64];
    char topic_buf[128];

    // 延时 5 秒启动，等待传感器完成首轮轮询
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
                
                // 提取 RTDB 中的快照数据
                cJSON_AddNumberToObject(dev_node, "online", reg_map_read_value(dev->status_tag_id));
                for (int j = 0; j < dev->rule_count; j++) {
                    cJSON_AddNumberToObject(dev_node, dev->rules[j].name, reg_map_read_value(dev->rules[j].target_tag_id));
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
    
    esp_transport_handle_t wiz_transport = esp_transport_wiznet_init();
    if (wiz_transport) {
        // 告诉底层 MQTT 引擎：只要遇到 wizmqtt:// 开头的地址，不要走 Wi-Fi，强制走 W5100S 硬件适配器！
        esp_mqtt_client_register_transport(g_mqtt_client, "wizmqtt", wiz_transport);
        ESP_LOGI(TAG, "Registered Custom Transport: wizmqtt:// mapped to W5100S Hardware TCP");
    }

    esp_mqtt_client_register_event(g_mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(g_mqtt_client);

    xTaskCreate(mqtt_tx_task, "mqtt_tx_worker", 4096, NULL, 5, NULL);
    xTaskCreate(mqtt_periodic_full_upload_task, "mqtt_full_snap", 6144, NULL, 4, NULL);
}