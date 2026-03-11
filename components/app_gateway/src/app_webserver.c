/**
 * @file app_webserver.c
 * @brief 应用层：RESTful API 与 Captive Portal 可视化融合前端
 * @note 内置无弹窗极简 UI 框架，整合 JSON 组态与 Wi-Fi 配网功能
 */
#include "app_webserver.h"
#include "config_manager.h"
#include "bsp_fs.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "WEB_API";

/* ============================================================
 * 融合前端 HTML (无 alert，精美原生双 Tab 设计)
 * ============================================================ */
static const char* INDEX_HTML = 
"<!DOCTYPE html>\n"
"<html>\n"
"<head>\n"
"  <meta charset=\"utf-8\">\n"
"  <title>IoT Edge Gateway 控制台</title>\n"
"  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n"
"  <style>\n"
"    body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Helvetica, Arial, sans-serif; background-color: #f3f4f6; padding: 20px; margin: 0; }\n"
"    .container { max-width: 800px; margin: 0 auto; background: #fff; padding: 24px; border-radius: 12px; box-shadow: 0 10px 15px -3px rgba(0,0,0,0.1); }\n"
"    .tabs { display: flex; border-bottom: 2px solid #e5e7eb; margin-bottom: 20px; }\n"
"    .tab { padding: 12px 24px; cursor: pointer; color: #6b7280; font-weight: 600; transition: 0.2s; border-bottom: 2px solid transparent; margin-bottom: -2px; }\n"
"    .tab:hover { color: #374151; }\n"
"    .tab.active { color: #2563eb; border-bottom: 2px solid #2563eb; }\n"
"    .content { display: none; }\n"
"    .content.active { display: block; }\n"
"    textarea, input { width: 100%; font-family: monospace; font-size: 14px; padding: 12px; border: 1px solid #d1d5db; border-radius: 6px; box-sizing: border-box; margin-bottom: 16px; }\n"
"    button { background-color: #2563eb; color: white; padding: 12px; border: none; border-radius: 6px; font-size: 16px; font-weight: bold; cursor: pointer; width: 100%; transition: 0.2s; }\n"
"    button:hover { background-color: #1d4ed8; }\n"
"    #toast { display: none; padding: 12px; border-radius: 6px; margin-bottom: 16px; font-weight: bold; text-align: center; }\n"
"  </style>\n"
"</head>\n"
"<body>\n"
"  <div class=\"container\">\n"
"    <h2 style=\"color: #111827; margin-top:0;\">边缘网关配置台</h2>\n"
"    <div id=\"toast\"></div>\n"
"    \n"
"    <div class=\"tabs\">\n"
"      <div class=\"tab active\" onclick=\"switchTab('json')\" id=\"tab-btn-json\">业务组态 (JSON)</div>\n"
"      <div class=\"tab\" onclick=\"switchTab('wifi')\" id=\"tab-btn-wifi\">厂区网络配网</div>\n"
"    </div>\n"
"\n"
"    <!-- 选项卡 1：业务组态 -->\n"
"    <div id=\"tab-json\" class=\"content active\">\n"
"      <p style=\"color: #6b7280; font-size: 14px;\">支持标准 Modbus 及非标指令配置映射，系统自带容错校验。</p>\n"
"      <textarea id=\"cfg\" style=\"height: 380px;\"></textarea>\n"
"      <button onclick=\"saveJson()\" id=\"btn-json\">校验并下发组态 (重启生效)</button>\n"
"    </div>\n"
"\n"
"    <!-- 选项卡 2：Wi-Fi 配网 -->\n"
"    <div id=\"tab-wifi\" class=\"content\">\n"
"      <p style=\"color: #6b7280; font-size: 14px;\">网关已开启临时管理热点，请在此填入厂区路由器信息。</p>\n"
"      <label style=\"font-weight: bold; font-size: 14px; color: #374151;\">Wi-Fi 名称 (SSID)</label>\n"
"      <input type=\"text\" id=\"wifi_ssid\" placeholder=\"输入厂区 Wi-Fi 名称\">\n"
"      <label style=\"font-weight: bold; font-size: 14px; color: #374151;\">Wi-Fi 密码</label>\n"
"      <input type=\"password\" id=\"wifi_pass\" placeholder=\"输入密码\">\n"
"      <button onclick=\"saveWifi()\" id=\"btn-wifi\" style=\"background-color: #059669;\">保存并移交网络 (连接厂区)</button>\n"
"    </div>\n"
"  </div>\n"
"\n"
"  <script>\n"
"    function showMsg(msg, isError) {\n"
"      let t = document.getElementById('toast');\n"
"      t.style.display = 'block';\n"
"      t.style.backgroundColor = isError ? '#fee2e2' : '#d1fae5';\n"
"      t.style.color = isError ? '#b91c1c' : '#047857';\n"
"      t.innerText = msg;\n"
"    }\n"
"\n"
"    function switchTab(target) {\n"
"      document.querySelectorAll('.content').forEach(el => el.classList.remove('active'));\n"
"      document.querySelectorAll('.tab').forEach(el => el.classList.remove('active'));\n"
"      document.getElementById('tab-' + target).classList.add('active');\n"
"      document.getElementById('tab-btn-' + target).classList.add('active');\n"
"    }\n"
"\n"
"    /* 页面加载拉取当前配置 */\n"
"    fetch('/api/config').then(r=>r.text()).then(d=>{ document.getElementById('cfg').value = d; });\n"
"    \n"
"    function saveJson(){\n"
"      let cfgText = document.getElementById('cfg').value;\n"
"      let parsedJson = null;\n"
"      try { parsedJson = JSON.parse(cfgText); } catch(e) {\n"
"        showMsg('❌ JSON 语法错误: ' + e.message, true); return;\n"
"      }\n"
"      \n"
"      let btn = document.getElementById('btn-json');\n"
"      btn.innerText = '配置下发中...'; btn.disabled = true;\n"
"      fetch('/api/config',{ method:'POST', body: JSON.stringify(parsedJson, null, 2) })\n"
"      .then(r=>{ if(r.ok) { showMsg('✅ 组态下发成功！网关正在硬重启...', false); }\n"
"                 else throw new Error(); })\n"
"      .catch(e=>{ showMsg('❌ 网络保存失败', true); btn.innerText='重试'; btn.disabled=false; });\n"
"    }\n"
"\n"
"    function saveWifi(){\n"
"      let s = document.getElementById('wifi_ssid').value;\n"
"      let p = document.getElementById('wifi_pass').value;\n"
"      if(!s) { showMsg('❌ SSID 不能为空', true); return; }\n"
"      \n"
"      let btn = document.getElementById('btn-wifi');\n"
"      btn.innerText = '正在联机，请勿刷新页面...'; btn.disabled = true;\n"
"      showMsg('🔄 设备正在尝试连接厂区路由，请等待...', false);\n"
"      \n"
"      fetch('/api/wifi',{ method:'POST', body: JSON.stringify({ssid: s, password: p}) })\n"
"      .then(r=>r.json()).then(data=>{\n"
"        showMsg('✅ 连接成功！\\n请将手机连回厂区Wi-Fi，通过局域网访问: http://gw-esp32.local', false);\n"
"        btn.style.display = 'none';\n"
"      })\n"
"      .catch(e=>{ showMsg('❌ 设备已重启或网络移交失败', true); });\n"
"    }\n"
"  </script>\n"
"</body>\n"
"</html>\n";

/* ============================================================
 * HTTP 路由拦截处理
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

// 接收前端配置并存入 VFS 文件系统持久化
static esp_err_t api_post_wifi_handler(httpd_req_t *req) {
    char *buf = malloc(req->content_len + 1);
    if (!buf) return HTTPD_SOCK_ERR_FAIL;
    int ret = httpd_req_recv(req, buf, req->content_len);
    if (ret <= 0) { free(buf); return HTTPD_SOCK_ERR_FAIL; }
    buf[ret] = '\0';

    cJSON *root = cJSON_Parse(buf);
    if (root) {
        bsp_fs_write_str_to_file("/vfs/wifi.json", buf);
        cJSON_Delete(root);
        
        // 响应客户端成功，提示其准备切换网络
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"status\": \"wifi_saved\"}");
        free(buf);
        
        ESP_LOGI(TAG, "New Wi-Fi credentials saved. Restarting to connect...");
        vTaskDelay(pdMS_TO_TICKS(2000)); // 给前端留出 2 秒展示动画的时间
        esp_restart(); // 彻底硬重启，交由底层的 bsp_wifi 自动连接新厂区路由
    } else {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "{\"error\": \"Bad JSON\"}");
        free(buf);
    }
    return ESP_OK;
}

// ==========================================================
// Captive Portal 核心漏洞拦截：
// 手机由于 DNS 劫持会发起各种乱七八糟的请求（如 /generate_204 等）
// 我们统一将其拦截为 302 重定向到网关主页
// ==========================================================
static esp_err_t captive_portal_redirect_handler(httpd_req_t *req, httpd_err_code_t err) {
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/"); // 引导手机跳转到根目录
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

/* ============================================================
 * 启动服务
 * ============================================================ */
void app_webserver_start(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 10;
    // 新增：LRU 淘汰机制。极大地增强 Captive Portal 应对手机并发轰炸的稳定性
    config.lru_purge_enable = true;
    httpd_handle_t server = NULL;
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t uri_root = { .uri = "/", .method = HTTP_GET, .handler = index_html_handler, .user_ctx = NULL };
        httpd_register_uri_handler(server, &uri_root);

        httpd_uri_t uri_get_config = { .uri = "/api/config", .method = HTTP_GET, .handler = api_get_config_handler };
        httpd_register_uri_handler(server, &uri_get_config);

        httpd_uri_t uri_post_config = { .uri = "/api/config", .method = HTTP_POST, .handler = api_post_config_handler };
        httpd_register_uri_handler(server, &uri_post_config);

        httpd_uri_t uri_post_wifi = { .uri = "/api/wifi", .method = HTTP_POST, .handler = api_post_wifi_handler };
        httpd_register_uri_handler(server, &uri_post_wifi);
        
        // 挂载全局错误拦截钩子，构建极其霸道的 Captive Portal 强制重定向
        httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, captive_portal_redirect_handler);
        
        ESP_LOGI(TAG, "Unified Config Portal started on port 80");
    } else {
        ESP_LOGE(TAG, "Failed to start Webserver!");
    }
}