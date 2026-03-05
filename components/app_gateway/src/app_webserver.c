/**
 * @file app_webserver.c
 * @brief 应用层：RESTful API 实现
 */
#include "app_webserver.h"
#include "config_manager.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include <string.h>

static const char *TAG = "WEB_API";

/* GET /api/config -> 读取配置文件并返回 JSON */
static esp_err_t api_get_config_handler(httpd_req_t *req) {
    char *json_str = config_manager_get_json();
    if (json_str) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, json_str);
        free(json_str);
    } else {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "{\"error\": \"Config not found\"}");
    }
    return ESP_OK;
}

/* POST /api/config -> 接收前端下发的 JSON 并保存，随后重启生效 */
static esp_err_t api_post_config_handler(httpd_req_t *req) {
    if (req->content_len > 8192) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "File too large");
        return ESP_FAIL;
    }

    char *buf = malloc(req->content_len + 1);
    int ret = httpd_req_recv(req, buf, req->content_len);
    if (ret <= 0) {
        free(buf);
        return HTTPD_SOCK_ERR_FAIL;
    }
    buf[ret] = '\0';

    if (config_manager_save_json(buf)) {
        httpd_resp_sendstr(req, "{\"status\": \"success\", \"msg\": \"Rebooting...\"}");
        ESP_LOGI(TAG, "Config saved. Restarting system in 2 seconds...");
        free(buf);
        vTaskDelay(pdMS_TO_TICKS(2000));
        esp_restart(); // 保存成功后立刻软重启，重新加载动态配置池
    } else {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "{\"error\": \"Invalid JSON Format\"}");
        free(buf);
    }
    return ESP_OK;
}

void app_webserver_start(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 8;
    
    httpd_handle_t server = NULL;
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t uri_get_config = {
            .uri      = "/api/config",
            .method   = HTTP_GET,
            .handler  = api_get_config_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &uri_get_config);

        httpd_uri_t uri_post_config = {
            .uri      = "/api/config",
            .method   = HTTP_POST,
            .handler  = api_post_config_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &uri_post_config);
        
        ESP_LOGI(TAG, "Webserver started on port %d", config.server_port);
    }
}