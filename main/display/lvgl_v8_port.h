/*
 * SPDX-FileCopyrightText: 2024-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */
#pragma once

#include "sdkconfig.h"
#include "esp_display_panel.hpp"
#include "lvgl.h"

// Compatibility helpers for LVGL 9 in C++ codebase
#ifdef __cplusplus
static inline lv_obj_t* lv_spinner_create(lv_obj_t* parent, uint32_t t, uint32_t angle) {
    lv_obj_t* sp = lv_spinner_create(parent);
    lv_spinner_set_anim_params(sp, t, angle);
    return sp;
}

#ifndef lv_event_get_target
#define lv_event_get_target(e) ((lv_obj_t*)lv_event_get_target(e))
#endif

#ifndef lv_event_get_current_target
#define lv_event_get_current_target(e) ((lv_obj_t*)lv_event_get_current_target(e))
#endif

#endif

#define LVGL_PORT_TICK_PERIOD_MS                (2)

#define LVGL_PORT_BUFFER_MALLOC_CAPS            (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#define LVGL_PORT_BUFFER_SIZE_HEIGHT            (512)
#define LVGL_PORT_BUFFER_NUM                    (2)

#define LVGL_PORT_TASK_MAX_DELAY_MS             (500)
#define LVGL_PORT_TASK_MIN_DELAY_MS             (2)
#define LVGL_PORT_TASK_STACK_SIZE               (16 * 1024)
#define LVGL_PORT_TASK_PRIORITY                 (2)
#define LVGL_PORT_TASK_CORE                     (1)

#ifdef __cplusplus
extern "C" {
#endif

bool lvgl_port_init(esp_panel::drivers::LCD *lcd, esp_panel::drivers::Touch *tp);
bool lvgl_port_deinit(void);
bool lvgl_port_lock(int timeout_ms);
bool lvgl_port_unlock(void);
/// Numero di frame completi inviati al display (per misure FPS)
uint32_t lvgl_port_get_frame_count(void);
/// Falsi rilasci del touch assorbiti (diagnostica, vedi touchpad_read)
uint32_t lvgl_port_touch_bridged_count(void);

#ifdef __cplusplus
}
#endif
