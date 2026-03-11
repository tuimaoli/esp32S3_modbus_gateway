/**
 * @file app_ota.h
 * @brief 应用层：系统 OTA 升级管理器
 * @note 封装底层 Flash 分区与写操作，提供高内聚的流式刷写接口，供 Web 或云端组件复用
 */
#pragma once

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    APP_OTA_OK = 0,
    APP_OTA_ERR_PARTITION,  // 分区查找失败
    APP_OTA_ERR_BEGIN,      // 初始化刷写句柄失败
    APP_OTA_ERR_WRITE,      // Flash 写入失败
    APP_OTA_ERR_VALIDATE,   // 固件完整性/MD5校验失败
    APP_OTA_ERR_BOOT_CFG    // 启动指针更新失败
} app_ota_err_t;

/**
 * @brief 开启 OTA 升级流程，锁定备用分区
 * @return APP_OTA_OK 表示就绪，可以开始写入
 */
app_ota_err_t app_ota_begin(void);

/**
 * @brief 流式写入固件块 (可循环调用)
 * @param data 二进制数据块指针
 * @param length 数据块长度
 * @return APP_OTA_OK 表示写入成功
 */
app_ota_err_t app_ota_write_chunk(const void *data, size_t length);

/**
 * @brief 结束 OTA 并校验固件，若成功则将下次启动指针指向新分区
 * @return APP_OTA_OK 表示升级彻底完成，可安全重启
 */
app_ota_err_t app_ota_end(void);

/**
 * @brief 中止 OTA 流程，释放资源 (用于异常中断兜底)
 */
void app_ota_abort(void);

#ifdef __cplusplus
}
#endif