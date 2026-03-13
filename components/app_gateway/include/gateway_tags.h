/**
 * @file gateway_tags.h
 * @brief 应用层：全局系统级测点 ID (Tag) 数据字典
 * @note 业务层的 JSON 动态点表应从 2000 起始，避开此处的保留号段
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * 1. 区域：本地 I/O 扩展芯片测点 (分配空间: 500 ~ 599)
 * ============================================================ */
#define TAG_ID_LOCAL_IO_INPUTS  500  ///< PCF8574 整体输入状态掩码 (Int32)
#define TAG_ID_LOCAL_RELAY_1    501  ///< 本地继电器 1 控制指令 (Bool)
#define TAG_ID_LOCAL_RELAY_2    502  ///< 本地继电器 2 控制指令 (Bool)

#define TAG_ID_LOCAL_AIN_0      510  ///< ADS1115 模拟通道 0 电压 (Float32)
#define TAG_ID_LOCAL_AIN_1      511  ///< ADS1115 模拟通道 1 电压 (Float32)
#define TAG_ID_LOCAL_AIN_2      512  ///< ADS1115 模拟通道 2 电压 (Float32)
#define TAG_ID_LOCAL_AIN_3      513  ///< ADS1115 模拟通道 3 电压 (Float32)

/* ============================================================
 * 2. 区域：系统级指示灯与按键测点 (分配空间: 1000 ~ 1099)
 * ============================================================ */
#define TAG_ID_SYS_BTN_RESET    1000 ///< 物理恢复出厂按键状态 (Bool)
#define TAG_ID_SYS_LED_1        1021 ///< 系统指示灯 1 (Bool)
#define TAG_ID_SYS_LED_2        1022 ///< 系统指示灯 2 (Bool)
#define TAG_ID_SYS_LED_3        1023 ///< 系统指示灯 3 (Bool)

#ifdef __cplusplus
}
#endif