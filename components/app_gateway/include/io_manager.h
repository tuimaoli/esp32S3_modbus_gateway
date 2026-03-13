/**
 * @file io_manager.h
 * @brief 应用层：本地 IO 与物理外设状态管理器
 * @note 负责将硬件状态（按键、LED、扩展IO、ADC）与虚拟的 RTDB 测点进行双向透明映射
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化本地 IO 管理器
 * @note 内部将完成 GPIO 配置、外设探测，并启动独立的 IO 轮询守护任务
 */
void io_manager_init(void);

#ifdef __cplusplus
}
#endif