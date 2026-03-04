/**
 * @file app_tcp_server.h
 * @brief 应用层：局域网 TCP 组态通讯服务端
 * @note 屏蔽了操作系统的差异，对外暴露启动接口
 */
#pragma once

/**
 * @brief 启动 W5100S 硬件 TCP 监听与查询任务
 * @note 任务内部包含死循环及 Socket 状态机，无需外部管理
 */
void app_tcp_server_start(void);