/**
 * @file app_webserver.c
 * @brief 应用层：RESTful API 与 可视化前端实现
 * @note 修复了 C 语言字符串拼接导致的前端单行注释吞噬代码 Bug，加入 \n 保障 JS 引擎正确解析
 */
#include "app_webserver.h"
#include "config_manager.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include <string.h>

static const char *TAG = "WEB_API";

/* ============================================================
 * 前端极简 HTML 页面 (带前端 JSON 格式拦截校验)
 * 采用 \n 换行保护，防止 C 编译器将代码压扁导致语法错误
 * ============================================================ */
static const char* INDEX_HTML = 
"<!DOCTYPE html>\n"
"<html>\n"
"<head>\n"
"  <meta charset=\"utf-8\">\n"
"  <title>IoT Gateway 组态后台</title>\n"
"  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n"
"</head>\n"
"<body style=\"font-family: Arial, sans-serif; padding: 20px; background-color: #f4f4f9;\">\n"
"  <div style=\"max-width: 800px; margin: 0 auto; background: #fff; padding: 20px; border-radius: 8px; box-shadow: 0 4px 6px rgba(0,0,0,0.1);\">\n"
"    <h2 style=\"color: #333;\">边缘网关动态组态配置</h2>\n"
"    <p style=\"color: #666; font-size: 14px;\">在此修改 JSON 配置。系统具备严格的格式校验功能。</p>\n"
"    <textarea id=\"cfg\" style=\"width: 100%; height: 400px; font-family: monospace; font-size: 14px; padding: 10px; border: 1px solid #ccc; border-radius: 4px; box-sizing: border-box;\"></textarea>\n"
"    <br><br>\n"
"    <button onclick=\"save()\" style=\"background-color: #007bff; color: white; padding: 10px 20px; border: none; border-radius: 4px; font-size: 16px; cursor: pointer; width: 100%; transition: 0.3s;\">校验并下发配置 (重启生效)</button>\n"
"  </div>\n"
"  <script>\n"
"    /* 页面加载时自动拉取现有配置 */\n"
"    fetch('/api/config').then(r=>r.text()).then(d=>{\n"
"      document.getElementById('cfg').value = d;\n"
"    });\n"
"    \n"
"    /* 核心逻辑：带拦截校验的保存函数 */\n"
"    function save(){\n"
"      let cfgText = document.getElementById('cfg').value;\n"
"      let parsedJson = null;\n"
"      \n"
"      /* 1. 严格的前端 JSON 格式校验防呆 */\n"
"      try {\n"
"        parsedJson = JSON.parse(cfgText);\n"
"      } catch(e) {\n"
"        alert('❌ JSON 格式错误，请检查语法！\\n\\n详细提示：' + e.message);\n"
"        return; /* 格式错误，直接截停，绝不向网关发送垃圾数据 */\n"
"      }\n"
"      \n"
"      /* 2. 格式化 JSON，让其更美观，然后再发给网关 */\n"
"      let formattedText = JSON.stringify(parsedJson, null, 2);\n"
"      document.getElementById('cfg').value = formattedText;\n"
"      \n"
"      /* 3. 安全下发 */\n"
"      let btn = document.querySelector('button');\n"
"      btn.innerText = '配置下发中...';\n"
"      btn.disabled = true;\n"
"      btn.style.backgroundColor = '#6c757d';\n"
"      fetch('/api/config',{\n"
"        method:'POST',\n"
"        body: formattedText\n"
"      }).then(r=>{\n"
"        if(r.ok) {\n"
"           alert('✅ 配置已成功下发！\\n网关正在硬重启，请在 3 秒后手动刷新页面。');\n"
"           btn.innerText = '设备重启中...';\n"
"        } else {\n"
"           throw new Error('Server returned ' + r.status);\n"
"        }\n"
"      }).catch(e=>{\n"
"        alert('❌ 网络错误，保存失败！');\n"
"        btn.innerText = '校验并下发配置 (重启生效)';\n"
"        btn.disabled = false;\n"
"        btn.style.backgroundColor = '#007bff';\n"
"      });\n"
"    }\n"
"  </script>\n"
"</body>\n"
"</html>\n";

/* ============================================================
 * HTTP 路由处理函数
 * ============================================================ */

static esp_err_t index_html_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr(req, INDEX_HTML);
    return ESP_OK;
}

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

static esp_err_t api_post_config_handler(httpd_req_t *req) {
    if (req->content_len > 8192) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "File too large");
        return ESP_FAIL;
    }

    char *buf = malloc(req->content_len + 1);
    if (!buf) return HTTPD_SOCK_ERR_FAIL;

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
        esp_restart(); 
    } else {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "{\"error\": \"Invalid JSON Format\"}");
        free(buf);
    }
    return ESP_OK;
}

/* ============================================================
 * 启动服务
 * ============================================================ */
void app_webserver_start(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 8;
    
    httpd_handle_t server = NULL;
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t uri_root = { .uri = "/", .method = HTTP_GET, .handler = index_html_handler, .user_ctx = NULL };
        httpd_register_uri_handler(server, &uri_root);

        httpd_uri_t uri_get_config = { .uri = "/api/config", .method = HTTP_GET, .handler = api_get_config_handler, .user_ctx = NULL };
        httpd_register_uri_handler(server, &uri_get_config);

        httpd_uri_t uri_post_config = { .uri = "/api/config", .method = HTTP_POST, .handler = api_post_config_handler, .user_ctx = NULL };
        httpd_register_uri_handler(server, &uri_post_config);
        
        ESP_LOGI(TAG, "Webserver started on port %d", config.server_port);
    } else {
        ESP_LOGE(TAG, "Failed to start Webserver!");
    }
}