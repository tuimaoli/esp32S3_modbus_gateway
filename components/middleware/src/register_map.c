/**
 * @file register_map.c
 * @brief 中间件层：实时数据库核心引擎实现 (含互斥锁)
 */
#include "register_map.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdlib.h>

static data_tag_t* g_tag_list_head = NULL;
static SemaphoreHandle_t g_reg_mutex = NULL;

static data_tag_t* find_tag_by_id_locked(uint16_t id) {
    data_tag_t* curr = g_tag_list_head;
    while (curr != NULL) {
        if (curr->id == id) 
            return curr;
        curr = curr->next;
    }
    return NULL;
}

void reg_map_init(void) {
    if (g_reg_mutex == NULL)
        g_reg_mutex = xSemaphoreCreateMutex();
}

bool reg_map_add_tag(uint16_t id, const char* name, tag_data_type_t type, bool persist) {
    if (g_reg_mutex == NULL)
        return false;

    data_tag_t* new_tag = (data_tag_t*)malloc(sizeof(data_tag_t));
    if (!new_tag)
        return false;

    memset(new_tag, 0, sizeof(data_tag_t));
    new_tag->id = id;
    strncpy(new_tag->name, name, sizeof(new_tag->name) - 1);
    new_tag->type = type;
    new_tag->persist = persist;
    new_tag->quality = TAG_QUAL_UNINITIALIZED;

    xSemaphoreTake(g_reg_mutex, portMAX_DELAY);
    if (g_tag_list_head == NULL) { 
        g_tag_list_head = new_tag;
    } else {
        data_tag_t* curr = g_tag_list_head;
        while (curr->next != NULL)
            curr = curr->next;
        curr->next = new_tag;
    }
    xSemaphoreGive(g_reg_mutex);
    return true;
}

bool reg_map_update_value(uint16_t id, float value) {
    if (g_reg_mutex == NULL) 
        return false;

    bool found = false;
    xSemaphoreTake(g_reg_mutex, portMAX_DELAY);
    data_tag_t* tag = find_tag_by_id_locked(id);
    if (tag != NULL) {
        tag->value.f_val = value;
        tag->quality = TAG_QUAL_GOOD;
        tag->last_update_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
        found = true;
    }
    xSemaphoreGive(g_reg_mutex);
    return found;
}

bool reg_map_update_quality(uint16_t id, tag_quality_t quality) {
    if (g_reg_mutex == NULL) 
        return false;

    bool found = false;
    xSemaphoreTake(g_reg_mutex, portMAX_DELAY);
    data_tag_t* tag = find_tag_by_id_locked(id);
    if (tag != NULL) { 
        tag->quality = quality; 
        found = true; 
    }
    xSemaphoreGive(g_reg_mutex);
    return found;
}

bool reg_map_get_value(uint16_t id, float* out_val, tag_quality_t* out_quality) {
    if (g_reg_mutex == NULL) 
        return false;

    bool found = false;
    xSemaphoreTake(g_reg_mutex, portMAX_DELAY);
    data_tag_t* tag = find_tag_by_id_locked(id);
    if (tag != NULL) {
        if (out_val) 
            *out_val = tag->value.f_val;
        if (out_quality) 
            *out_quality = tag->quality;
        found = true;
    }
    xSemaphoreGive(g_reg_mutex);
    return found;
}