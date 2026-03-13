/**
 * @file app_linkage.h
 * @brief 应用层：边缘联动逻辑引擎 (Soft-PLC Core)
 * @note 定期扫描 RTDB 执行条件判断，触发本地跨设备控制操作
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 启动边缘联动引擎守护任务
 */
void app_linkage_start(void);

#ifdef __cplusplus
}
#endif