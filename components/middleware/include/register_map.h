/**
 * @file register_map.h
 * @brief 中间件层：微型实时数据库 (RTDB) V4.0
 * @note 引入工业级质量戳 (Quality)、权限控制 (Writable) 与 NVS 掉电记忆机制
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 工业级测点质量戳 (Quality Stamp) - 严格遵循 1~4 状态机映射
 */
typedef enum {
    TAG_QUAL_INIT            = 0, ///< 初始化状态 (刚上电暂无数据)
    TAG_QUAL_GOOD            = 1, ///< 在线且通讯极佳
    TAG_QUAL_WEAK_RETRY      = 2, ///< 通讯弱/丢包，正在底层的重试机制中
    TAG_QUAL_OFFLINE_TIMEOUT = 3, ///< 超时彻底离线 (此时测点值保留最后有效值，但质量变差)
    TAG_QUAL_SENSOR_ERR      = 4  ///< 通讯正常，但传感器返回了硬件错误码 (如 Modbus 0x80)
} tag_quality_t;

/**
 * @brief 测点数据类型字典
 */
typedef enum {
    TAG_TYPE_BOOL = 0,
    TAG_TYPE_INT32,
    TAG_TYPE_FLOAT32
} tag_type_t;

/**
 * @brief 动态测点注册配置表 (V4.0 高级属性)
 */
typedef struct {
    char        name[32];
    tag_type_t  type;
    bool        writable;      ///< 是否允许北向(云端/联动引擎)写入
    bool        persist;       ///< 是否开启 NVS 掉电记忆 (如报警阈值、灯光状态)
    uint16_t    reverse_reg;   ///< 反向写入寄存器映射地址 (0xFFFF表示无物理映射)
    uint8_t     slave_id;      ///< 物理路由地址 (供底层反向下发指令使用)
} rtdb_tag_cfg_t;

/**
 * @brief 初始化 RTDB 中枢 (内部包含 NVS 初始化与互斥锁建立)
 */
void reg_map_init(void);

/**
 * @brief 极简注册测点 (向下兼容老版本，默认不可写、不记忆)
 * @param tag_id 全局唯一测点 ID
 * @param name 测点名称
 * @param type 数据类型
 * @param read_only 是否只读
 */
void reg_map_add_tag(uint16_t tag_id, const char *name, tag_type_t type, bool read_only);

/**
 * @brief 高级注册测点 (V4.0，支持读写与掉电记忆配置)
 * @param tag_id 全局唯一测点 ID
 * @param cfg 高级配置结构体指针
 */
void reg_map_add_tag_ext(uint16_t tag_id, const rtdb_tag_cfg_t *cfg);

/**
 * @brief 南向更新：底端采集引擎刷入最新工程值
 */
void reg_map_update_value(uint16_t tag_id, float value);

/**
 * @brief 南向更新：底端采集引擎刷入最新质量戳 (解决断线不归零问题)
 */
void reg_map_update_quality(uint16_t tag_id, tag_quality_t quality);

/**
 * @brief 北向读取：供 MQTT、WebServer 或联动引擎获取最新值与质量
 * @param tag_id 测点 ID
 * @param out_value 接收值的指针 (允许为 NULL)
 * @param out_quality 接收质量戳的指针 (允许为 NULL)
 * @return true 测点存在, false 测点不存在
 */
bool reg_map_get_value(uint16_t tag_id, float *out_value, tag_quality_t *out_quality);

/**
 * @brief 北向写入：供云端或上位机下发控制指令
 * @note 内部将自动处理写入权限校验、NVS 持久化记忆以及触发反向 TX 队列
 * @return true 写入成功, false 写入失败(测点不存在或不可写)
 */
bool reg_map_write_value(uint16_t tag_id, float new_value);

#ifdef __cplusplus
}
#endif