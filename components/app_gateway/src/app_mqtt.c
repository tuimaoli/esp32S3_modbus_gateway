/**
 * @file app_mqtt.c
 * @brief MQTT 高级业务实现 (修复版：解决传输层注册报错问题)
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

// #include "transport_wiznet.h" // 暂时隐蔽自定义硬件传输层

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
        ESP_LOGI(TAG, "MQTT RX: Topic=%.*s", event->topic_len, event->topic);
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
    
    /* * [架构注释] 
     * 由于 ESP-IDF v4/v5 官方 MQTT 客户端屏蔽了动态注入外部 Transport 的接口，
     * 此处隐蔽原有的 esp_transport_wiznet_init 逻辑。
     * 当前系统将自动通过 LwIP (Wi-Fi 软栈) 进行 MQTT 数据上报。
     * 若后期强需求走 W5100S 硬件栈上报 MQTT，建议引入独立的 coreMQTT 或 Paho-MQTT 纯 C 库。
     */
    // esp_transport_handle_t wiz_transport = esp_transport_wiznet_init();
    // if (wiz_transport) {
    //     esp_mqtt_client_register_transport(g_mqtt_client, "wizmqtt", wiz_transport);
    // }

    esp_mqtt_client_register_event(g_mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(g_mqtt_client);

    xTaskCreate(mqtt_tx_task, "mqtt_tx_worker", 4096, NULL, 5, NULL);
    xTaskCreate(mqtt_periodic_full_upload_task, "mqtt_full_snap", 6144, NULL, 4, NULL);
}