/**
 * @file utils_expr.h
 * @brief 中间件层：轻量级表达式计算引擎 (递归下降解析器)
 * @note 支持浮点四则、逻辑运算、比较运算、括号嵌套及数学函数。纯净实现，完全解耦。
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 变量解析回调函数类型
 * @param tag_id 解析出的测点 ID (如遇到 TAG(3001) 时 tag_id 为 3001)
 * @param out_val 用于带回该测点的实时值
 * @return true: 测点存在且数据有效(在线); false: 数据无效，将导致整个表达式求值中止
 */
typedef bool (*expr_var_cb_t)(uint16_t tag_id, float *out_val);

/**
 * @brief 执行字符串表达式求值
 * @param expr_str 表达式字符串 (例如: "TAG(3001) > 35.0 && ABS(TAG(3002) - 10) <= 2.5")
 * @param out_result 运算结果。若是逻辑判断，>0.5f 视为 true，否则为 false
 * @param var_cb 获取 TAG 变量实时值的外部钩子函数
 * @return true 求值成功, false 求值失败 (语法错误、括号不匹配或获取变量失败)
 */
bool utils_expr_eval(const char *expr_str, float *out_result, expr_var_cb_t var_cb);

#ifdef __cplusplus
}
#endif