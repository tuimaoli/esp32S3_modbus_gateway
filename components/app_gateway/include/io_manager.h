/**
 * @file io_manager.h
 * @brief 应用层：本地扩展 I/O 管理器接口
 * @note 封装了对外置 I/O 扩展芯片 (如 PCF8574) 的业务逻辑控制
 */
#pragma once

/**
 * @brief 启动本地 I/O 硬件引脚的轮询映射任务
 * @note 任务会将实际物理电平与 RTDB 虚拟标签(Tag)进行双向同步
 */
void io_manager_init(void);