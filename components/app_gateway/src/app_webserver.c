/**
 * @file app_webserver.c
 * @brief 应用层：RESTful API 与 Captive Portal 可视化融合前端
 */
#include "app_webserver.h"
#include "config_manager.h"
#include "bsp_fs.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_app_desc.h"  // ⚡ 引入固件描述结构体头文件
#include "app_ota.h"       
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <sys/param.h>     // 修复 MIN 宏报错的关键头文件

static const char *TAG = "WEB_API";

// 引用底层的 Smart Handoff 测试接口
extern bool bsp_wifi_try_connect_and_get_ip(const char* ssid, const char* pass, char* out_ip, uint32_t timeout_ms);

/* ============================================================
 * 融合前端 HTML (保持不变)
 * ============================================================ */
static const char* INDEX_HTML = 
"<!DOCTYPE html>\n"
"<html>\n"
"<head>\n"
"  <meta charset=\"utf-8\">\n"
"  <title>IoT Edge Gateway 控制台</title>\n"
"  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n"
"  <style>\n"
"    body { font-family: -apple-system, BlinkMacSystemFont, sans-serif; background-color: #f3f4f6; padding: 20px; margin: 0; }\n"
"    .container { max-width: 800px; margin: 0 auto; background: #fff; padding: 24px; border-radius: 12px; box-shadow: 0 10px 15px -3px rgba(0,0,0,0.1); }\n"
"    .tabs { display: flex; border-bottom: 2px solid #e5e7eb; margin-bottom: 20px; overflow-x: auto; }\n"
"    .tab { padding: 12px 24px; cursor: pointer; color: #6b7280; font-weight: 600; transition: 0.2s; border-bottom: 2px solid transparent; margin-bottom: -2px; white-space: nowrap; }\n"
"    .tab.active { color: #2563eb; border-bottom: 2px solid #2563eb; }\n"
"    .content { display: none; }\n"
"    .content.active { display: block; }\n"
"    textarea, input { width: 100%; font-family: monospace; font-size: 14px; padding: 12px; border: 1px solid #d1d5db; border-radius: 6px; box-sizing: border-box; margin-bottom: 16px; }\n"
"    button { background-color: #2563eb; color: white; padding: 12px; border: none; border-radius: 6px; font-size: 16px; font-weight: bold; cursor: pointer; width: 100%; }\n"
"    button:disabled { background-color: #9ca3af; cursor: not-allowed; }\n"
"    #toast { display: none; padding: 12px; border-radius: 6px; margin-bottom: 16px; font-weight: bold; text-align: center; }\n"
"    a { color: #2563eb; text-decoration: underline; }\n"
"    .progress-bar { width: 100%; background-color: #e5e7eb; border-radius: 6px; overflow: hidden; display: none; margin-bottom: 16px; }\n"
"    .progress-fill { height: 12px; background-color: #059669; width: 0%; transition: width 0.2s; }\n"
"  </style>\n"
"</head>\n"
"<body>\n"
"  <div class=\"container\">\n"
"    <h2 style=\"margin-top:0;\">边缘网关配置台</h2>\n"
"    <div id=\"toast\"></div>\n"
"    <div class=\"tabs\">\n"
"      <div class=\"tab active\" onclick=\"switchTab('json')\" id=\"tab-btn-json\">业务组态</div>\n"
"      <div class=\"tab\" onclick=\"switchTab('wifi')\" id=\"tab-btn-wifi\">网络移交</div>\n"
"      <div class=\"tab\" onclick=\"switchTab('ota')\" id=\"tab-btn-ota\">系统升级(OTA)</div>\n"
"    </div>\n"
"    \n"
"    <!-- 1. 业务组态 -->\n"
"    <div id=\"tab-json\" class=\"content active\">\n"
"      <textarea id=\"cfg\" style=\"height: 380px;\"></textarea>\n"
"      <button onclick=\"saveJson()\" id=\"btn-json\">校验并下发组态</button>\n"
"    </div>\n"
"    \n"
"    <!-- 2. 网络配网 -->\n"
"    <div id=\"tab-wifi\" class=\"content\">\n"
"      <label>Wi-Fi 名称 (SSID)</label>\n"
"      <input type=\"text\" id=\"wifi_ssid\" placeholder=\"输入厂区 Wi-Fi\">\n"
"      <label>Wi-Fi 密码</label>\n"
"      <input type=\"password\" id=\"wifi_pass\" placeholder=\"输入密码\">\n"
"      <button onclick=\"saveWifi()\" id=\"btn-wifi\" style=\"background-color: #059669;\">保存并获取 IP</button>\n"
"    </div>\n"
"    \n"
"    <!-- 3. OTA 升级 -->\n"
"    <div id=\"tab-ota\" class=\"content\">\n"
"      <p style=\"color: #6b7280; font-size: 14px;\">请选择最新的 `.bin` 固件文件。升级过程中请勿断开电源。</p>\n"
"      <input type=\"file\" id=\"ota_file\" accept=\".bin\" style=\"padding: 8px 0;\">\n"
"      <div class=\"progress-bar\" id=\"ota_progress_bg\"><div class=\"progress-fill\" id=\"ota_progress\"></div></div>\n"
"      <button onclick=\"startOTA()\" id=\"btn-ota\" style=\"background-color: #dc2626;\">开始刷写固件</button>\n"
"    </div>\n"
"  </div>\n"
"  \n"
"  <script>\n"
"    function showMsg(msg, isError) {\n"
"      let t = document.getElementById('toast');\n"
"      t.style.display = 'block'; t.innerHTML = msg;\n"
"      t.style.backgroundColor = isError ? '#fee2e2' : '#d1fae5';\n"
"      t.style.color = isError ? '#b91c1c' : '#047857';\n"
"    }\n"
"    function switchTab(t) {\n"
"      document.querySelectorAll('.content, .tab').forEach(el => el.classList.remove('active'));\n"
"      document.getElementById('tab-' + t).classList.add('active');\n"
"      document.getElementById('tab-btn-' + t).classList.add('active');\n"
"    }\n"
"    fetch('/api/config').then(r=>r.text()).then(d=>{ document.getElementById('cfg').value = d; });\n"
"    \n"
"    function saveJson(){\n"
"      let parsed = null; try { parsed = JSON.parse(document.getElementById('cfg').value); } catch(e) { showMsg('❌ JSON 错误', true); return; }\n"
"      fetch('/api/config',{ method:'POST', body: JSON.stringify(parsed, null, 2) })\n"
"      .then(r=>{ if(r.ok) showMsg('✅ 下发成功！网关重启中...', false); })\n"
"    }\n"
"    \n"
"    function saveWifi(){\n"
"      let s = document.getElementById('wifi_ssid').value, p = document.getElementById('wifi_pass').value;\n"
"      if(!s) return;\n"
"      let btn = document.getElementById('btn-wifi');\n"
"      btn.innerText = '设备正在联机，请等待 5-15 秒...'; btn.disabled = true;\n"
"      showMsg('🔄 正在穿透网络获取 IP，请勿关闭页面...', false);\n"
"      \n"
"      fetch('/api/wifi',{ method:'POST', body: JSON.stringify({ssid: s, password: p}) })\n"
"      .then(r=>r.json()).then(data=>{\n"
"        if(data.status === 'success') {\n"
"           showMsg('✅ 配网成功！设备新 IP 为: <br><br><a href=\"http://' + data.ip + '\" style=\"font-size:24px;\">http://' + data.ip + '</a>', false);\n"
"           btn.style.display = 'none';\n"
"        } else {\n"
"           showMsg('❌ 连接厂区路由器失败。', true);\n"
"           btn.innerText = '保存并获取 IP'; btn.disabled = false;\n"
"        }\n"
"      }).catch(e=>{ showMsg('❌ 请求断开', true); btn.innerText = '重试'; btn.disabled = false; });\n"
"    }\n"
"    \n"
"    function startOTA() {\n"
"      let fileInput = document.getElementById('ota_file');\n"
"      if (fileInput.files.length === 0) { showMsg('❌ 请先选择 .bin 文件', true); return; }\n"
"      let file = fileInput.files[0];\n"
"      \n"
"      let btn = document.getElementById('btn-ota');\n"
"      let progBg = document.getElementById('ota_progress_bg');\n"
"      let progFill = document.getElementById('ota_progress');\n"
"      btn.disabled = true; btn.innerText = '正在上传固件...';\n"
"      progBg.style.display = 'block'; progFill.style.width = '0%';\n"
"      showMsg('⏳ 正在刷写系统底包，过程可能持续 30-60 秒，切勿断电！', false);\n"
"\n"
"      let xhr = new XMLHttpRequest();\n"
"      xhr.upload.addEventListener('progress', function(e) {\n"
"        if (e.lengthComputable) {\n"
"          let percent = Math.round((e.loaded / e.total) * 100);\n"
"          progFill.style.width = percent + '%';\n"
"          if (percent === 100) btn.innerText = '正在校验并写入 Flash...';\n"
"        }\n"
"      });\n"
"      xhr.onreadystatechange = function() {\n"
"        if (xhr.readyState === 4) {\n"
"          if (xhr.status === 200) {\n"
"             showMsg('🎉 升级成功！网关正在硬重启，请等待 10 秒后刷新页面。', false);\n"
"             btn.innerText = '升级完成';\n"
"             setTimeout(() => { window.location.reload(); }, 10000);\n"
"          } else {\n"
"             showMsg('❌ 升级失败，文件校验错误或网络中断', true);\n"
"             btn.disabled = false; btn.innerText = '重新尝试';\n"
"          }\n"
"        }\n"
"      };\n"
"      xhr.open('POST', '/api/ota', true);\n"
"      xhr.setRequestHeader('Content-Type', 'application/octet-stream');\n"
"      xhr.send(file);\n"
"    }\n"
"  </script>\n"
"</body>\n"
"</html>\n";

/* ============================================================
 * 原有 HTTP 路由处理模块
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

// ⚡ 新增：获取底层固件真实烙印信息的 API
static esp_err_t api_get_sysinfo_handler(httpd_req_t *req) {
    const esp_app_desc_t *app_desc = esp_app_get_description();
    char resp[256];
    // 将编译时自动生成的 版本号、日期、时间 组装返回
    snprintf(resp, sizeof(resp), 
             "{\"firmware_ver\": \"%s\", \"compile_date\": \"%s %s\", \"idf_ver\": \"%s\"}",
             app_desc->version, app_desc->date, app_desc->time, app_desc->idf_ver);
             
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, resp);
    return ESP_OK;
}

static esp_err_t api_post_config_handler(httpd_req_t *req) {
    char *buf = malloc(req->content_len + 1);
    if (!buf) return HTTPD_SOCK_ERR_FAIL;
    int ret = httpd_req_recv(req, buf, req->content_len);
    if (ret <= 0) { free(buf); return HTTPD_SOCK_ERR_FAIL; }
    buf[ret] = '\0';

    if (config_manager_save_json(buf)) {
        httpd_resp_sendstr(req, "{\"status\": \"success\"}");
        free(buf);
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_restart(); 
    } else {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "{\"error\": \"Invalid JSON\"}");
        free(buf);
    }
    return ESP_OK;
}

static esp_err_t api_post_wifi_handler(httpd_req_t *req) {
    char *buf = malloc(req->content_len + 1);
    if (!buf) return HTTPD_SOCK_ERR_FAIL;
    int ret = httpd_req_recv(req, buf, req->content_len);
    if (ret <= 0) { free(buf); return HTTPD_SOCK_ERR_FAIL; }
    buf[ret] = '\0';

    cJSON *root = cJSON_Parse(buf);
    if (root) {
        char *ssid = cJSON_GetObjectItem(root, "ssid")->valuestring;
        char *pass = cJSON_GetObjectItem(root, "password")->valuestring;

        bsp_fs_write_str_to_file("/vfs/wifi.json", buf);
        char new_ip[32] = {0};
        bool success = bsp_wifi_try_connect_and_get_ip(ssid, pass, new_ip, 15000);
        
        httpd_resp_set_type(req, "application/json");
        if (success) {
            char resp[128];
            snprintf(resp, sizeof(resp), "{\"status\": \"success\", \"ip\": \"%s\"}", new_ip);
            httpd_resp_sendstr(req, resp);
        } else {
            httpd_resp_sendstr(req, "{\"status\": \"fail\", \"error\": \"Connection Failed\"}");
        }
        
        cJSON_Delete(root);
        free(buf);
        vTaskDelay(pdMS_TO_TICKS(3000)); 
        esp_restart(); 
    } else {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "{\"error\": \"Bad JSON\"}");
        free(buf);
    }
    return ESP_OK;
}

static esp_err_t api_post_ota_handler(httpd_req_t *req) {
    if (app_ota_begin() != APP_OTA_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA Begin Failed");
        return ESP_FAIL;
    }

    char buf[1024]; 
    int received = 0;
    int remaining = req->content_len;
    
    while (remaining > 0) {
        int recv_len = httpd_req_recv(req, buf, MIN(remaining, sizeof(buf)));
        if (recv_len <= 0) {
            if (recv_len == HTTPD_SOCK_ERR_TIMEOUT) continue; 
            ESP_LOGE(TAG, "HTTP RX Error!");
            app_ota_abort(); 
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Rx Error");
            return ESP_FAIL;
        }
        
        if (app_ota_write_chunk(buf, recv_len) != APP_OTA_OK) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Flash Write Error");
            return ESP_FAIL;
        }
        
        remaining -= recv_len;
        received += recv_len;
        vTaskDelay(pdMS_TO_TICKS(1)); 
    }

    if (app_ota_end() != APP_OTA_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA Validation Failed");
        return ESP_FAIL;
    }
    
    httpd_resp_sendstr(req, "{\"status\": \"success\"}");
    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();
    return ESP_OK;
}

static esp_err_t captive_portal_redirect_handler(httpd_req_t *req, httpd_err_code_t err) {
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/"); 
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

void app_webserver_start(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 10;
    config.lru_purge_enable = true;
    
    httpd_handle_t server = NULL;
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t uri_root = { .uri = "/", .method = HTTP_GET, .handler = index_html_handler };
        httpd_register_uri_handler(server, &uri_root);

        // 注册 sysinfo 路由
        httpd_uri_t uri_get_sysinfo = { .uri = "/api/sysinfo", .method = HTTP_GET, .handler = api_get_sysinfo_handler };
        httpd_register_uri_handler(server, &uri_get_sysinfo);

        httpd_uri_t uri_get_config = { .uri = "/api/config", .method = HTTP_GET, .handler = api_get_config_handler };
        httpd_register_uri_handler(server, &uri_get_config);

        httpd_uri_t uri_post_config = { .uri = "/api/config", .method = HTTP_POST, .handler = api_post_config_handler };
        httpd_register_uri_handler(server, &uri_post_config);

        httpd_uri_t uri_post_wifi = { .uri = "/api/wifi", .method = HTTP_POST, .handler = api_post_wifi_handler };
        httpd_register_uri_handler(server, &uri_post_wifi);
        
        httpd_uri_t uri_post_ota = { .uri = "/api/ota", .method = HTTP_POST, .handler = api_post_ota_handler };
        httpd_register_uri_handler(server, &uri_post_ota);
        
        httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, captive_portal_redirect_handler);
        
        ESP_LOGI(TAG, "Unified Config Portal started on port 80");
    }
}