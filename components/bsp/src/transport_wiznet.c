/**
 * @file transport_wiznet.c
 * @brief BSP层：WIZnet 传输适配器实现 (降维打击，直操寄存器避开 POSIX 冲突)
 */
#include "transport_wiznet.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>

#ifdef MR
#undef MR
#endif
#include "Ethernet/wizchip_conf.h"

static const char *TAG = "TRANS_WIZ";
#define SOCK_MQTT_W5100S 2 // 预留 Socket 2 给 MQTT 专线使用

/* ============================================================
 * 传输层函数多态实现 (Implementation of Interface)
 * ============================================================ */

static int wiznet_connect(esp_transport_handle_t t, const char *host, int port, int timeout_ms) {
    uint8_t target_ip[4] = {0};
    
    // 简易 DNS：直接解析 IPv4 字符串 (如果是域名，需配合 WIZnet DNS 解析器)
    if (sscanf(host, "%hhu.%hhu.%hhu.%hhu", &target_ip[0], &target_ip[1], &target_ip[2], &target_ip[3]) != 4) {
        ESP_LOGE(TAG, "W5100S currently supports IP address only for MQTT host: %s", host);
        return -1;
    }

    // 1. 初始化 Socket
    setSn_CR(SOCK_MQTT_W5100S, Sn_CR_CLOSE);
    while(getSn_CR(SOCK_MQTT_W5100S));
    setSn_IR(SOCK_MQTT_W5100S, 0xFF); 

    // 2. 配置 TCP 模式与动态本地端口
    uint16_t dynamic_port = 40000 + (xTaskGetTickCount() % 10000);
    setSn_MR(SOCK_MQTT_W5100S, Sn_MR_TCP);
    setSn_PORT(SOCK_MQTT_W5100S, dynamic_port);
    setSn_CR(SOCK_MQTT_W5100S, Sn_CR_OPEN);
    while(getSn_CR(SOCK_MQTT_W5100S));

    // 3. 发起硬件 TCP 连接
    setSn_DIPR(SOCK_MQTT_W5100S, target_ip);
    setSn_DPORT(SOCK_MQTT_W5100S, port);
    setSn_CR(SOCK_MQTT_W5100S, Sn_CR_CONNECT);
    while(getSn_CR(SOCK_MQTT_W5100S));

    // 4. 阻塞等待连接成功或超时
    int elapsed = 0;
    while (getSn_SR(SOCK_MQTT_W5100S) != SOCK_ESTABLISHED && elapsed < timeout_ms) {
        vTaskDelay(pdMS_TO_TICKS(10));
        elapsed += 10;
        if (getSn_SR(SOCK_MQTT_W5100S) == SOCK_CLOSED) return -1;
    }
    
    return (getSn_SR(SOCK_MQTT_W5100S) == SOCK_ESTABLISHED) ? 0 : -1;
}

static int wiznet_read(esp_transport_handle_t t, char *buffer, int len, int timeout_ms) {
    int rx_len = getSn_RX_RSR(SOCK_MQTT_W5100S);
    if (rx_len > 0) {
        if (rx_len > len) rx_len = len;
        wiz_recv_data(SOCK_MQTT_W5100S, (uint8_t*)buffer, rx_len);
        setSn_CR(SOCK_MQTT_W5100S, Sn_CR_RECV);
        while(getSn_CR(SOCK_MQTT_W5100S));
        return rx_len;
    }
    return 0; // 无数据
}

static int wiznet_write(esp_transport_handle_t t, const char *buffer, int len, int timeout_ms) {
    if (getSn_SR(SOCK_MQTT_W5100S) != SOCK_ESTABLISHED) return -1;
    
    // 检查硬件发送缓存是否充足
    uint16_t freesize = 0;
    while (freesize < len) {
        freesize = getSn_TX_FSR(SOCK_MQTT_W5100S);
        if (getSn_SR(SOCK_MQTT_W5100S) != SOCK_ESTABLISHED) return -1;
    }

    wiz_send_data(SOCK_MQTT_W5100S, (uint8_t*)buffer, len);
    setSn_CR(SOCK_MQTT_W5100S, Sn_CR_SEND);
    while(getSn_CR(SOCK_MQTT_W5100S));
    return len;
}

static int wiznet_close(esp_transport_handle_t t) {
    setSn_CR(SOCK_MQTT_W5100S, Sn_CR_DISCON);
    while(getSn_CR(SOCK_MQTT_W5100S));
    setSn_CR(SOCK_MQTT_W5100S, Sn_CR_CLOSE);
    while(getSn_CR(SOCK_MQTT_W5100S));
    return 0;
}

static int wiznet_poll_read(esp_transport_handle_t t, int timeout_ms) {
    int elapsed = 0;
    while (elapsed < timeout_ms) {
        if (getSn_RX_RSR(SOCK_MQTT_W5100S) > 0) return 1; // 硬件显存有数据可读
        if (getSn_SR(SOCK_MQTT_W5100S) != SOCK_ESTABLISHED) return -1; // 连接断开
        vTaskDelay(pdMS_TO_TICKS(10));
        elapsed += 10;
    }
    return 0; // 超时
}

static int wiznet_poll_write(esp_transport_handle_t t, int timeout_ms) {
    return (getSn_SR(SOCK_MQTT_W5100S) == SOCK_ESTABLISHED) ? 1 : -1;
}

static esp_err_t wiznet_destroy(esp_transport_handle_t t) {
    return ESP_OK;
}

/* ============================================================
 * 注册器
 * ============================================================ */
esp_transport_handle_t esp_transport_wiznet_init(void) {
    esp_transport_handle_t t = esp_transport_init();
    if (t == NULL) return NULL;
    
    // 将 WIZnet 硬件操作注册到 ESP-IDF 传输抽象层
    esp_transport_set_func(t, wiznet_connect, wiznet_read, wiznet_write, wiznet_close, 
                           wiznet_poll_read, wiznet_poll_write, wiznet_destroy);
    return t;
}