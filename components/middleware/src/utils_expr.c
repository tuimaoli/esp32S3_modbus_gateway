/**
 * @file utils_expr.c
 * @brief 中间件层：轻量级表达式计算引擎实现
 * @note 采用严格的算符优先级的递归下降算法 (Recursive Descent Parsing)
 * 优先级: ()/TAG/ABS/ROUND > 单目(!,-) > 乘除模(*,/,%) > 加减(+,-) > 关系(>,<,>=,<=) > 相等(==,!=) > 逻辑与(&&) > 逻辑或(||)
 */
#include "utils_expr.h"
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <math.h>

// 内部解析器前向声明
static float parse_or(const char **s, expr_var_cb_t cb, bool *err);

// 辅助函数：跳过空白字符
static inline void skip_spaces(const char **s) {
    while (isspace((unsigned char)**s)) (*s)++;
}

// 辅助函数：匹配特定字符串并跳过
static bool match_str(const char **s, const char *target) {
    skip_spaces(s);
    size_t len = strlen(target);
    if (strncmp(*s, target, len) == 0) {
        *s += len;
        return true;
    }
    return false;
}

// 8. 基础因子层: 解析数字, TAG(), ABS(), ROUND() 和 括号 ()
static float parse_primary(const char **s, expr_var_cb_t cb, bool *err) {
    skip_spaces(s);
    if (*err) return 0.0f;

    // 解析括号
    if (**s == '(') {
        (*s)++;
        float val = parse_or(s, cb, err);
        skip_spaces(s);
        if (**s == ')') {
            (*s)++;
        } else {
            *err = true; // 括号不匹配
        }
        return val;
    }
    
    // 解析内置函数 TAG(id)
    if (match_str(s, "TAG")) {
        skip_spaces(s);
        if (**s != '(') { *err = true; return 0.0f; }
        (*s)++;
        float id_val = parse_or(s, cb, err);
        skip_spaces(s);
        if (**s != ')') { *err = true; return 0.0f; }
        (*s)++;
        
        float tag_val = 0.0f;
        if (cb == NULL || !cb((uint16_t)id_val, &tag_val)) {
            *err = true; // 变量获取失败 (比如传感器离线)，直接阻断求值
            return 0.0f;
        }
        return tag_val;
    }
    
    // 解析绝对值 ABS(expr)
    if (match_str(s, "ABS")) {
        skip_spaces(s);
        if (**s != '(') { *err = true; return 0.0f; }
        (*s)++;
        float val = parse_or(s, cb, err);
        skip_spaces(s);
        if (**s != ')') { *err = true; return 0.0f; }
        (*s)++;
        return fabsf(val);
    }
    
    // 解析取整 ROUND(expr)
    if (match_str(s, "ROUND")) {
        skip_spaces(s);
        if (**s != '(') { *err = true; return 0.0f; }
        (*s)++;
        float val = parse_or(s, cb, err);
        skip_spaces(s);
        if (**s != ')') { *err = true; return 0.0f; }
        (*s)++;
        return roundf(val);
    }

    // 解析数字常量
    char *end_ptr;
    float val = strtof(*s, &end_ptr);
    if (end_ptr == *s) {
        *err = true; // 既不是数字也不是合法符号
        return 0.0f;
    }
    *s = end_ptr;
    return val;
}

// 7. 单目运算层: ! (非), - (负号)
static float parse_unary(const char **s, expr_var_cb_t cb, bool *err) {
    skip_spaces(s);
    if (**s == '!') {
        (*s)++;
        float val = parse_unary(s, cb, err);
        return (val > 0.5f) ? 0.0f : 1.0f; // 逻辑非
    } else if (**s == '-') {
        (*s)++;
        return -parse_unary(s, cb, err);   // 负号
    }
    return parse_primary(s, cb, err);
}

// 6. 乘除模运算层: *, /, %
static float parse_mul(const char **s, expr_var_cb_t cb, bool *err) {
    float val = parse_unary(s, cb, err);
    while (!*err) {
        skip_spaces(s);
        if (**s == '*') {
            (*s)++; val *= parse_unary(s, cb, err);
        } else if (**s == '/') {
            (*s)++; 
            float divisor = parse_unary(s, cb, err);
            if (fabsf(divisor) < 1e-6) { *err = true; return 0.0f; } // 除零保护
            val /= divisor;
        } else if (**s == '%') {
            (*s)++; 
            float divisor = parse_unary(s, cb, err);
            if (fabsf(divisor) < 1e-6) { *err = true; return 0.0f; }
            val = fmodf(val, divisor); // 浮点取模
        } else {
            break;
        }
    }
    return val;
}

// 5. 加减运算层: +, -
static float parse_add(const char **s, expr_var_cb_t cb, bool *err) {
    float val = parse_mul(s, cb, err);
    while (!*err) {
        skip_spaces(s);
        if (**s == '+') {
            (*s)++; val += parse_mul(s, cb, err);
        } else if (**s == '-') {
            (*s)++; val -= parse_mul(s, cb, err);
        } else {
            break;
        }
    }
    return val;
}

// 4. 关系运算层: >, <, >=, <=
static float parse_rel(const char **s, expr_var_cb_t cb, bool *err) {
    float val = parse_add(s, cb, err);
    while (!*err) {
        skip_spaces(s);
        if (match_str(s, ">=")) {
            val = (val >= parse_add(s, cb, err)) ? 1.0f : 0.0f;
        } else if (match_str(s, "<=")) {
            val = (val <= parse_add(s, cb, err)) ? 1.0f : 0.0f;
        } else if (**s == '>') {
            (*s)++; val = (val > parse_add(s, cb, err)) ? 1.0f : 0.0f;
        } else if (**s == '<') {
            (*s)++; val = (val < parse_add(s, cb, err)) ? 1.0f : 0.0f;
        } else {
            break;
        }
    }
    return val;
}

// 3. 相等运算层: ==, !=
static float parse_eq(const char **s, expr_var_cb_t cb, bool *err) {
    float val = parse_rel(s, cb, err);
    while (!*err) {
        skip_spaces(s);
        if (match_str(s, "==")) {
            val = (fabsf(val - parse_rel(s, cb, err)) < 1e-5) ? 1.0f : 0.0f;
        } else if (match_str(s, "!=")) {
            val = (fabsf(val - parse_rel(s, cb, err)) >= 1e-5) ? 1.0f : 0.0f;
        } else {
            break;
        }
    }
    return val;
}

// 2. 逻辑与运算层: &&
static float parse_and(const char **s, expr_var_cb_t cb, bool *err) {
    float val = parse_eq(s, cb, err);
    while (!*err) {
        skip_spaces(s);
        if (match_str(s, "&&")) {
            float next_val = parse_eq(s, cb, err);
            val = ((val > 0.5f) && (next_val > 0.5f)) ? 1.0f : 0.0f;
        } else {
            break;
        }
    }
    return val;
}

// 1. 逻辑或运算层: || (顶层入口)
static float parse_or(const char **s, expr_var_cb_t cb, bool *err) {
    float val = parse_and(s, cb, err);
    while (!*err) {
        skip_spaces(s);
        if (match_str(s, "||")) {
            float next_val = parse_and(s, cb, err);
            val = ((val > 0.5f) || (next_val > 0.5f)) ? 1.0f : 0.0f;
        } else {
            break;
        }
    }
    return val;
}

// ----------------------------------------------------
// 引擎主入口
// ----------------------------------------------------
bool utils_expr_eval(const char *expr_str, float *out_result, expr_var_cb_t var_cb) {
    if (!expr_str || !out_result) return false;
    
    bool has_error = false;
    const char *p = expr_str;
    
    float result = parse_or(&p, var_cb, &has_error);
    
    skip_spaces(&p);
    // 如果解析完后字符串没到结尾，说明有未识别的乱码，也视为失败
    if (has_error || *p != '\0') {
        return false;
    }
    
    *out_result = result;
    return true;
}