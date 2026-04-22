/**
 * @file bsp_sc16is750.c
 * @brief BSP层：SC16IS750 拓展串口驱动实现
 * @note 实现了 bsp_serial_port_t 多态接口，自动开启芯片硬件 RS485 换向功能
 */
#include "bsp_sc16is750.h"
#include "bsp_i2c.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "SC16IS750";

/* SC16IS750 核心寄存器地址 (Channel A) */
#define SC_REG_RHR      0x00 // RX Holding Reg (Read)
#define SC_REG_THR      0x00 // TX Holding Reg (Write)
#define SC_REG_IER      0x01 // Interrupt Enable
#define SC_REG_FCR      0x02 // FIFO Control
#define SC_REG_LCR      0x03 // Line Control
#define SC_REG_MCR      0x04 // Modem Control
#define SC_REG_LSR      0x05 // Line Status
#define SC_REG_MSR      0x06 // Modem Status
#define SC_REG_SPR      0x07 // Scratchpad
#define SC_REG_TXLVL    0x08 // TX FIFO Level
#define SC_REG_RXLVL    0x09 // RX FIFO Level
#define SC_REG_EFCR     0x0F // Extra Features Control

/* 波特率分频寄存器 (需 LCR[7] = 1 才能访问) */
#define SC_REG_DLL      0x00 // Divisor Latch LSB
#define SC_REG_DLH      0x01 // Divisor Latch MSB

#define SC_LCR_DLAB_BIT 0x80

/* 私有上下文结构体 */
typedef struct {
    int i2c_port;
    uint8_t i2c_addr;
} sc16is750_ctx_t;

/* ============================================================
 * 内部 I2C 寄存器读写辅助函数
 * ============================================================ */
static uint8_t sc_read_reg(sc16is750_ctx_t *ctx, uint8_t reg) {
    uint8_t reg_addr = (reg << 3); // 芯片要求寄存器地址左移 3 位 (Channel A)
    uint8_t val = 0;
    
    // I2C Write-Read 序列
    bsp_i2c_write(ctx->i2c_port, ctx->i2c_addr, &reg_addr, 1);
    bsp_i2c_read(ctx->i2c_port, ctx->i2c_addr, &val, 1);
    return val;
}

static void sc_write_reg(sc16is750_ctx_t *ctx, uint8_t reg, uint8_t val) {
    uint8_t buf[2];
    buf[0] = (reg << 3);
    buf[1] = val;
    bsp_i2c_write(ctx->i2c_port, ctx->i2c_addr, buf, 2);
}

/* ============================================================
 * OOP 接口函数实现
 * ============================================================ */

static int sc_send(bsp_serial_port_t *port, const uint8_t *data, size_t len) {
    sc16is750_ctx_t *ctx = (sc16is750_ctx_t *)port->priv_data;
    size_t sent = 0;
    
    while (sent < len) {
        // 检查 TX FIFO 剩余空间 (SC16IS750 有 64 字节 FIFO)
        uint8_t tx_lvl = sc_read_reg(ctx, SC_REG_TXLVL);
        
        if (tx_lvl > 0) {
            // 写入一个字节
            sc_write_reg(ctx, SC_REG_THR, data[sent++]);
        } else {
            // FIFO 满，短暂让出 CPU
            vTaskDelay(pdMS_TO_TICKS(1)); 
        }
    }
    return sent;
}

static int sc_recv(bsp_serial_port_t *port, uint8_t *buf, size_t max_len, uint32_t timeout_ms) {
    sc16is750_ctx_t *ctx = (sc16is750_ctx_t *)port->priv_data;
    size_t received = 0;
    uint32_t start_tick = xTaskGetTickCount();
    uint32_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);
    
    while (received < max_len) {
        // 检查 RX FIFO 缓存内是否有数据
        uint8_t rx_lvl = sc_read_reg(ctx, SC_REG_RXLVL);
        
        if (rx_lvl > 0) {
            buf[received++] = sc_read_reg(ctx, SC_REG_RHR);
            // 每收到一个字节刷新超时器，防止报文截断
            start_tick = xTaskGetTickCount(); 
        } else {
            if ((xTaskGetTickCount() - start_tick) >= timeout_ticks) {
                break; // 超时退出
            }
            vTaskDelay(pdMS_TO_TICKS(2)); // 轮询节流
        }
    }
    return received;
}

static void sc_flush(bsp_serial_port_t *port) {
    sc16is750_ctx_t *ctx = (sc16is750_ctx_t *)port->priv_data;
    // 设置 FCR 寄存器: 重置 TX 和 RX FIFO (位 1 和 位 2 置 1)
    sc_write_reg(ctx, SC_REG_FCR, 0x06);
}

/* ============================================================
 * 工厂函数：创建实例
 * ============================================================ */

bsp_serial_port_t* bsp_sc16is750_create(int logical_port_id, int i2c_port, uint8_t i2c_addr, int baud_rate, uint32_t xtal_freq_hz) {
    // 1. 分配面向对象的内存结构
    bsp_serial_port_t *port = (bsp_serial_port_t *)calloc(1, sizeof(bsp_serial_port_t));
    sc16is750_ctx_t *ctx = (sc16is750_ctx_t *)calloc(1, sizeof(sc16is750_ctx_t));
    
    if (!port || !ctx) {
        if(port) free(port);
        if(ctx) free(ctx);
        return NULL;
    }
    
    ctx->i2c_port = i2c_port;
    ctx->i2c_addr = i2c_addr;
    
    // 2. 绑定虚函数表 (vtable) 和 上下文
    port->port_id = logical_port_id;
    port->send = sc_send;
    port->recv = sc_recv;
    port->flush = sc_flush;
    port->priv_data = ctx;
    
    // 3. 硬件芯片初始化配置
    // 计算波特率分频系数: Divisor = XTAL / (Baudrate * 16)
    uint16_t divisor = (uint16_t)(xtal_freq_hz / (baud_rate * 16));
    
    // 打开 LCR DLAB 位，以配置波特率
    sc_write_reg(ctx, SC_REG_LCR, SC_LCR_DLAB_BIT);
    sc_write_reg(ctx, SC_REG_DLL, divisor & 0xFF);
    sc_write_reg(ctx, SC_REG_DLH, (divisor >> 8) & 0xFF);
    
    // 8数据位，1停止位，无校验 (常规 Modbus 标准)
    sc_write_reg(ctx, SC_REG_LCR, 0x03);
    
    // 开启 FIFO
    sc_write_reg(ctx, SC_REG_FCR, 0x01);
    
    // ⚡开启硬件 RS485 自动方向控制 (EFCR 寄存器 bit4 = 1)
    // 芯片会自动在发送数据时拉高 DIR 脚，发送完毕拉低
    sc_write_reg(ctx, SC_REG_EFCR, 0x10); 
    
    // 清空缓存
    sc_flush(port);
    
    ESP_LOGI(TAG, "SC16IS750 Port %d initialized on I2C Addr 0x%02X at %d Baud", logical_port_id, i2c_addr, baud_rate);
    return port;
}