/**
 * @file bsp_ads1115.c
 * @brief BSP层：ADS1115 驱动实现
 * @note 严格消除魔法数字，包含寄存器查表解码与安全的阻塞等待转换逻辑
 */

#include "bsp_ads1115.h"
#include "bsp_i2c.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// ADS1115 内部寄存器指针定义
#define ADS_REG_POINTER_CONVERT   0x00
#define ADS_REG_POINTER_CONFIG    0x01

// 查找表：根据 PGA 枚举得出 1 LSB 对应的物理电压 (伏特)
static const float ADS_PGA_LSB_VOLTAGE[] = {
    6.144f / 32768.0f,  // 0: +/- 6.144V
    4.096f / 32768.0f,  // 1: +/- 4.096V
    2.048f / 32768.0f,  // 2: +/- 2.048V
    1.024f / 32768.0f,  // 3: +/- 1.024V
    0.512f / 32768.0f,  // 4: +/- 0.512V
    0.256f / 32768.0f   // 5: +/- 0.256V
};

bool bsp_ads1115_read_single_ended(int i2c_port, uint8_t i2c_addr, uint8_t channel, ads1115_pga_e pga, float *out_voltage) 
{
    if (channel > 3 || out_voltage == NULL) {
        return false;
    }

    // ==========================================
    // 1. 拼装 Config 寄存器配置报文 (16-bit)
    // ==========================================
    uint16_t config = 0;
    config |= (1 << 15);                           // OS: 1 = 开始单次转换
    config |= ((4 + channel) << 12);               // MUX: 100~111 代表 AIN0~AIN3 相对 GND 的单端读取
    config |= (pga << 9);                          // PGA: 增益设置
    config |= (1 << 8);                            // MODE: 1 = 单次转换模式 (节电)
    config |= (4 << 5);                            // DR: 100 = 128 SPS 速率 (抗工频干扰最佳组合)
    config |= 0x0003;                              // COMP_QUE: 11 = 禁用比较器

    // 发送 Config 写入指令
    uint8_t cfg_cmd[3];
    cfg_cmd[0] = ADS_REG_POINTER_CONFIG;
    cfg_cmd[1] = (config >> 8) & 0xFF;
    cfg_cmd[2] = config & 0xFF;
    
    if (bsp_i2c_write(i2c_port, i2c_addr, cfg_cmd, 3) != 0) {
        return false;
    }

    // ==========================================
    // 2. 等待 ADC 转换完成 (128SPS 大约需 8ms)
    // ==========================================
    vTaskDelay(pdMS_TO_TICKS(10)); 

    // ==========================================
    // 3. 读取 Conversion 结果寄存器
    // ==========================================
    uint8_t ptr_cmd = ADS_REG_POINTER_CONVERT;
    if (bsp_i2c_write(i2c_port, i2c_addr, &ptr_cmd, 1) != 0) {
        return false;
    }

    uint8_t rx_buf[2] = {0};
    if (bsp_i2c_read(i2c_port, i2c_addr, rx_buf, 2) != 0) {
        return false;
    }

    // 解码并转换为物理电压
    int16_t raw_adc = (rx_buf[0] << 8) | rx_buf[1];
    *out_voltage = (float)raw_adc * ADS_PGA_LSB_VOLTAGE[pga];

    return true;
}