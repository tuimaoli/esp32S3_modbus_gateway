/**
 * @file bsp_fs.h
 * @brief BSP层：LittleFS 文件系统抽象接口
 * @note 负责网关配置文件的持久化读写，提供安全的字符串流访问
 */
#pragma once
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 挂载 LittleFS 分区
 * @return ESP_OK 成功; 其他 失败
 */
esp_err_t bsp_fs_init(void);

/**
 * @brief 将文件完整读取为字符串 (动态分配内存，需使用者 free)
 * @param path 文件绝对路径，如 "/vfs/config.json"
 * @return 成功返回以 '\0' 结尾的字符串指针，失败返回 NULL
 */
char* bsp_fs_read_file_to_str(const char* path);

/**
 * @brief 将字符串覆盖写入到指定文件
 * @param path 文件绝对路径
 * @param content 要写入的字符串
 * @return ESP_OK 成功; 其他 失败
 */
esp_err_t bsp_fs_write_str_to_file(const char* path, const char* content);

#ifdef __cplusplus
}
#endif