/**
 * @file bsp_w5100s.h
 * @brief BSP层：W5100S 纯硬件以太网桥接驱动抽象接口
 * @note 采用 SPI 接口与 ESP32 通信，负责初始化 WIZnet 硬件状态机
 */
#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "driver/spi_master.h"

/**
 * @brief W5100S 硬件引脚与 SPI 总线配置描述符
 */
typedef struct {
    spi_host_device_t host_id;  ///< SPI 主机号 (例如 SPI2_HOST)
    int mosi_io;                ///< SPI MOSI 数据发送引脚号
    int miso_io;                ///< SPI MISO 数据接收引脚号
    int sclk_io;                ///< SPI SCLK 时钟引脚号
    int cs_io;                  ///< SPI CS   片选引脚号 (低电平有效)
    int rst_io;                 ///< 硬件复位引脚号 (传入 -1 表示不使用硬件复位)
    int clock_speed_mhz;        ///< SPI 通信时钟频率 (单位: MHz，建议 5~20)
} bsp_w5100s_config_t;

/**
 * @brief 初始化 W5100S 硬件与网络参数 (静态 IP 配置)
 * @param config W5100S 硬件引脚配置结构体指针
 * @return ESP_OK: 成功; 其他: 初始化失败
 */
esp_err_t bsp_w5100s_init(const bsp_w5100s_config_t *config);