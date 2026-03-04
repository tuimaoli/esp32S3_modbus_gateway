/**
 * @file register_map.h
 * @brief 中间件层：基于链表的高性能实时数据库 (RTDB) 引擎
 * @note 采用面向对象思维封装了测点(Tag)生命周期，替代传统的数组映射，支持离线判定与高并发
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 数据质量枚举 (判断设备离线/在线的核心依据)
 */
typedef enum {
    TAG_QUAL_GOOD = 0,       ///< 数据正常，设备在线且通信成功
    TAG_QUAL_BAD_TIMEOUT,    ///< 传感器超时离线 (轮询无响应)
    TAG_QUAL_BAD_SENSOR_ERR, ///< 传感器返回故障码 (通信正常但硬件故障)
    TAG_QUAL_UNINITIALIZED   ///< 系统刚启动，尚未采集到有效数据
} tag_quality_t;

/**
 * @brief 数据类型枚举 (支持多种物理量格式转换)
 */
typedef enum {
    TAG_TYPE_INT16 = 0,      ///< 有符号 16 位整数
    TAG_TYPE_UINT16,         ///< 无符号 16 位整数 (多数 Modbus 寄存器默认格式)
    TAG_TYPE_INT32,          ///< 有符号 32 位整数
    TAG_TYPE_FLOAT32,        ///< 单精度浮点数 (如 IEEE754 标准)
    TAG_TYPE_BOOL            ///< 布尔开关量 (如继电器、线圈状态)
} tag_data_type_t;

/**
 * @brief 测点对象 (Data Tag) 描述符
 * @note 承载了一个物理量在网关内部的完整生命周期状态
 */
typedef struct data_tag {
    uint16_t id;                ///< 统一逻辑 ID (对外通讯的唯一标识，原虚拟寄存器地址)
    char name[32];              ///< 测点名称 (如 "Pump_Temp"，便于调试和未来组态)
    tag_data_type_t type;       ///< 本测点的数据类型
    bool persist;               ///< 标志位：是否需要断电保存 (如设备运行时间、累计流量等存入 NVS)
    
    /** @brief 实时状态数据载体 */
    union {
        int32_t i_val;
        float f_val;
        bool b_val;
    } value;
    
    tag_quality_t quality;      ///< 当前数据质量戳 (TCP/MQTT客户端依此判断数据有效性)
    uint32_t last_update_ms;    ///< 最后一次成功更新的时间戳 (系统运行毫秒数)
    
    struct data_tag* next;      ///< 内部单向链表指针，用于 RTDB 遍历
} data_tag_t;

/**
 * @brief 初始化实时数据库
 * @note 会在内部创建 FreeRTOS Mutex 互斥锁，保障多核多线程下的读写安全
 */
void reg_map_init(void);

/**
 * @brief 动态添加一个测点 (注册到 RTDB 中)
 * @param id 统一逻辑 ID
 * @param name 测点名称描述
 * @param type 数据类型
 * @param persist 是否需要断电保存到 NVS
 * @return true: 添加成功, false: 内存不足或获取锁失败
 */
bool reg_map_add_tag(uint16_t id, const char* name, tag_data_type_t type, bool persist);

/**
 * @brief 更新测点数值 (通常由采集任务/生产者调用)
 * @note 调用此函数会自动将 quality 置为 TAG_QUAL_GOOD，并刷新 last_update_ms
 * @param id 目标测点的逻辑 ID
 * @param value 最新的物理量数值
 * @return true: 更新成功, false: 未找到该 ID
 */
bool reg_map_update_value(uint16_t id, float value);

/**
 * @brief 强行更新测点质量状态 (常用于设备超时、掉线判定)
 * @param id 目标测点的逻辑 ID
 * @param quality 新的质量戳 (如 TAG_QUAL_BAD_TIMEOUT)
 * @return true: 更新成功, false: 未找到该 ID
 */
bool reg_map_update_quality(uint16_t id, tag_quality_t quality);

/**
 * @brief 读取测点当前数值与质量 (通常由发布任务/消费者调用)
 * @param id 目标测点的逻辑 ID
 * @param[out] out_val 用于接收数值的指针 (传 NULL 则不读此项)
 * @param[out] out_quality 用于接收质量的指针 (传 NULL 则不读此项)
 * @return true: 测点存在并成功读取, false: 未找到该 ID
 */
bool reg_map_get_value(uint16_t id, float* out_val, tag_quality_t* out_quality);

#ifdef __cplusplus
}
#endif