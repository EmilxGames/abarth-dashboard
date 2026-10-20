/*
 * SPDX-FileCopyrightText: 2024-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include "lvgl_v8_port.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <esp_timer.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_rom_sys.h>
#include <cstring>
#include <algorithm>
#include "config/settings.h"

using namespace esp_panel::drivers;

static const char *TAG = "lvgl_port";

static SemaphoreHandle_t lvgl_mux = nullptr;
static TaskHandle_t lvgl_task_handle = nullptr;
static esp_timer_handle_t lvgl_tick_timer = nullptr;
static SemaphoreHandle_t touch_detected = nullptr;

static uint16_t g_rotation_degree = 0;
static uint16_t *g_slice_buf = nullptr;
static void *lvgl_buf[LVGL_PORT_BUFFER_NUM] = { nullptr };

static lv_display_t *g_disp = nullptr;
static lv_indev_t *g_indev = nullptr;

// Fast L1 SRAM scratchpad for tiled rotation (4 KB total, static in internal DRAM BSS)
alignas(16) static uint16_t s_tile_in[32][32];
alignas(16) static uint16_t s_tile_out[32][32];

// Cache-blocked 32x32 rotation algorithm with contiguous burst PSRAM I/O
IRAM_ATTR static void rotate_slice_16bpp(
    const uint16_t *src, uint16_t *dst,
    int w, int h, uint16_t rotate
)
{
    constexpr int BX = 32;
    constexpr int BY = 32;

    if (rotate == 90) {
        for (int x0 = 0; x0 < w; x0 += BX) {
            const int bw = (x0 + BX <= w) ? BX : (w - x0);
            for (int y0 = 0; y0 < h; y0 += BY) {
                const int bh = (y0 + BY <= h) ? BY : (h - y0);

                // 1. Sequential burst read from PSRAM into L1 SRAM
                for (int ty = 0; ty < bh; ty++) {
                    memcpy(s_tile_in[ty], &src[(y0 + ty) * w + x0], bw * sizeof(uint16_t));
                }

                // 2. Transpose in fast L1 SRAM
                for (int tx = 0; tx < bw; tx++) {
                    for (int ty = 0; ty < bh; ty++) {
                        s_tile_out[tx][bh - 1 - ty] = s_tile_in[ty][tx];
                    }
                }

                // 3. Sequential burst write to PSRAM from L1 SRAM
                for (int tx = 0; tx < bw; tx++) {
                    const int dst_offset = (x0 + tx) * h + (h - y0 - bh);
                    memcpy(&dst[dst_offset], s_tile_out[tx], bh * sizeof(uint16_t));
                }
            }
        }
    } else if (rotate == 270) {
        for (int x0 = 0; x0 < w; x0 += BX) {
            const int bw = (x0 + BX <= w) ? BX : (w - x0);
            for (int y0 = 0; y0 < h; y0 += BY) {
                const int bh = (y0 + BY <= h) ? BY : (h - y0);

                // 1. Sequential burst read from PSRAM into L1 SRAM
                for (int ty = 0; ty < bh; ty++) {
                    memcpy(s_tile_in[ty], &src[(y0 + ty) * w + x0], bw * sizeof(uint16_t));
                }

                // 2. Transpose in fast L1 SRAM
                for (int tx = 0; tx < bw; tx++) {
                    for (int ty = 0; ty < bh; ty++) {
                        s_tile_out[tx][ty] = s_tile_in[ty][tx];
                    }
                }

                // 3. Sequential burst write to PSRAM from L1 SRAM
                for (int tx = 0; tx < bw; tx++) {
                    const int dst_offset = (w - 1 - (x0 + tx)) * h + y0;
                    memcpy(&dst[dst_offset], s_tile_out[tx], bh * sizeof(uint16_t));
                }
            }
        }
    }
}

static void safe_draw_bitmap(LCD *lcd, int x, int y, int w, int h, const uint8_t *data)
{
    int retries = 0;
    while (retries < 10) {
        // timeout_ms = 50 sincronizza con il semaforo DMA interno del driver LCD MIPI-DSI
        if (lcd->drawBitmap(x, y, w, h, data, 50)) {
            break;
        }
        retries++;
        esp_rom_delay_us(500);
    }
}

static void flush_callback(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    LCD *lcd = (LCD *)lv_display_get_user_data(disp);
    const int x1 = area->x1;
    const int x2 = area->x2;
    const int y1 = area->y1;
    const int y2 = area->y2;
    const int w = x2 - x1 + 1;
    const int h = y2 - y1 + 1;

    if (g_rotation_degree != 0) {
        if (!g_slice_buf) {
            g_slice_buf = (uint16_t *)heap_caps_malloc(1024 * 600 * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (g_slice_buf) {
                memset(g_slice_buf, 0x00, 1024 * 600 * sizeof(uint16_t));
            }
        }

        if (g_slice_buf) {
            int64_t t0 = esp_timer_get_time();
            rotate_slice_16bpp((const uint16_t *)px_map, g_slice_buf, w, h, g_rotation_degree);
            int64_t t_rot = esp_timer_get_time() - t0;

            int phys_x, phys_y, phys_w, phys_h;
            if (g_rotation_degree == 90) {
                phys_x = 1024 - 1 - y2;
                phys_y = x1;
                phys_w = h;
                phys_h = w;
            } else { // 270
                phys_x = y1;
                phys_y = 600 - 1 - x2;
                phys_w = h;
                phys_h = w;
            }

            if (phys_x < 0) phys_x = 0;
            if (phys_y < 0) phys_y = 0;
            if (phys_x + phys_w > 1024) phys_w = 1024 - phys_x;
            if (phys_y + phys_h > 600) phys_h = 600 - phys_y;

            if (phys_w > 0 && phys_h > 0) {
                int64_t t1 = esp_timer_get_time();
                safe_draw_bitmap(lcd, phys_x, phys_y, phys_w, phys_h, (const uint8_t *)g_slice_buf);
                int64_t t_draw = esp_timer_get_time() - t1;

                static int s_flush_count = 0;
                if ((++s_flush_count % 30) == 0) {
                    ESP_LOGI("FLUSH_PERF", "(%dx%d) rot=%.2f ms, draw=%.2f ms",
                             w, h, (float)t_rot / 1000.0f, (float)t_draw / 1000.0f);
                }
            }
        } else {
            safe_draw_bitmap(lcd, x1, y1, w, h, px_map);
        }
    } else {
        safe_draw_bitmap(lcd, x1, y1, w, h, px_map);
    }

    lv_display_flush_ready(disp);
}

static void touchpad_read(lv_indev_t *indev, lv_indev_data_t *data)
{
    Touch *tp = (Touch *)lv_indev_get_user_data(indev);
    TouchPoint point;
    data->state = LV_INDEV_STATE_RELEASED;

    if (tp->isInterruptEnabled() && (xSemaphoreTake(touch_detected, 0) == pdFALSE)) {
        return;
    }

    int read_touch_result = tp->readPoints(&point, 1, 0);
    if (read_touch_result > 0) {
        int tx = point.x;
        int ty = point.y;

        if (g_rotation_degree == 90) {
            data->point.x = ty;
            data->point.y = 1023 - tx;
        } else if (g_rotation_degree == 270) {
            data->point.x = 599 - ty;
            data->point.y = tx;
        } else {
            data->point.x = tx;
            data->point.y = ty;
        }

        lv_display_t *disp = lv_indev_get_display(indev);
        int max_x = lv_display_get_horizontal_resolution(disp) - 1;
        int max_y = lv_display_get_vertical_resolution(disp) - 1;
        if (data->point.x < 0) data->point.x = 0;
        if (data->point.y < 0) data->point.y = 0;
        if (data->point.x > max_x) data->point.x = max_x;
        if (data->point.y > max_y) data->point.y = max_y;

        data->state = LV_INDEV_STATE_PRESSED;
    }
}

static bool onTouchInterruptCallback(void *user_data)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xSemaphoreGiveFromISR(touch_detected, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    return false;
}

static lv_display_t *display_init(LCD *lcd)
{
    if (!lcd || !lcd->getRefreshPanelHandle()) {
        ESP_LOGE(TAG, "LCD device non valido o non inizializzato");
        return nullptr;
    }

    int32_t log_w = (g_rotation_degree == 90 || g_rotation_degree == 270) ? 600 : 1024;
    int32_t log_h = (g_rotation_degree == 90 || g_rotation_degree == 270) ? 1024 : 600;

    lv_display_t *disp = lv_display_create(log_w, log_h);
    if (!disp) {
        ESP_LOGE(TAG, "lv_display_create fallito");
        return nullptr;
    }

    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_user_data(disp, (void *)lcd);

    uint32_t buffer_pixels = log_w * LVGL_PORT_BUFFER_SIZE_HEIGHT;
    uint32_t buffer_bytes = buffer_pixels * sizeof(uint16_t);

    for (int i = 0; i < LVGL_PORT_BUFFER_NUM; i++) {
        lvgl_buf[i] = heap_caps_malloc(buffer_bytes, LVGL_PORT_BUFFER_MALLOC_CAPS);
        if (!lvgl_buf[i]) {
            ESP_LOGE(TAG, "Allocazione buffer LVGL %d fallita (%u bytes)", i, (unsigned)buffer_bytes);
            return nullptr;
        }
        ESP_LOGI(TAG, "Buffer LVGL %d allocato: %p (%u bytes)", i, lvgl_buf[i], (unsigned)buffer_bytes);
    }

    lv_display_set_buffers(disp, lvgl_buf[0], lvgl_buf[1], buffer_bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(disp, flush_callback);

    lv_timer_t *refr_timer = lv_display_get_refr_timer(disp);
    if (refr_timer) {
        lv_timer_set_period(refr_timer, 16); // 62.5 FPS display refresh target
    }

    return disp;
}

static lv_indev_t *indev_init(Touch *tp, lv_display_t *disp)
{
    if (!tp || !tp->getPanelHandle()) {
        ESP_LOGE(TAG, "Touch device non valido o non inizializzato");
        return nullptr;
    }

    if (tp->isInterruptEnabled()) {
        touch_detected = xSemaphoreCreateBinary();
        tp->attachInterruptCallback(onTouchInterruptCallback, tp);
    }

    lv_indev_t *indev = lv_indev_create();
    if (!indev) {
        ESP_LOGE(TAG, "lv_indev_create fallito");
        return nullptr;
    }

    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touchpad_read);
    lv_indev_set_user_data(indev, (void *)tp);
    lv_indev_set_display(indev, disp);

    lv_timer_t *read_timer = lv_indev_get_read_timer(indev);
    if (read_timer) {
        lv_timer_set_period(read_timer, 15); // 66.7 Hz responsive touch reading
    }

    return indev;
}

static void tick_increment(void *arg)
{
    lv_tick_inc(LVGL_PORT_TICK_PERIOD_MS);
}

static bool tick_init(void)
{
    const esp_timer_create_args_t lvgl_tick_timer_args = {
        .callback = &tick_increment,
        .name = "LVGL tick"
    };
    if (esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer) != ESP_OK) {
        ESP_LOGE(TAG, "Creazione timer tick LVGL fallita");
        return false;
    }
    if (esp_timer_start_periodic(lvgl_tick_timer, LVGL_PORT_TICK_PERIOD_MS * 1000) != ESP_OK) {
        ESP_LOGE(TAG, "Avvio timer tick LVGL fallito");
        return false;
    }
    return true;
}

static void lvgl_port_task(void *arg)
{
    ESP_LOGI(TAG, "LVGL task avviato su Core 1");

    uint32_t task_delay_ms = LVGL_PORT_TASK_MAX_DELAY_MS;
    while (1) {
        if (lvgl_port_lock(-1)) {
            task_delay_ms = lv_timer_handler();
            lvgl_port_unlock();
        }
        if (task_delay_ms > LVGL_PORT_TASK_MAX_DELAY_MS) {
            task_delay_ms = LVGL_PORT_TASK_MAX_DELAY_MS;
        } else if (task_delay_ms < LVGL_PORT_TASK_MIN_DELAY_MS) {
            task_delay_ms = LVGL_PORT_TASK_MIN_DELAY_MS;
        }
        vTaskDelay(pdMS_TO_TICKS(task_delay_ms));
    }
}

bool lvgl_port_init(LCD *lcd, Touch *tp)
{
    if (!lcd) {
        ESP_LOGE(TAG, "Parametro LCD nullo");
        return false;
    }

    lvgl_mux = xSemaphoreCreateRecursiveMutex();
    if (!lvgl_mux) {
        ESP_LOGE(TAG, "Creazione mutex LVGL fallita");
        return false;
    }

    lv_init();

    if (!tick_init()) {
        ESP_LOGE(TAG, "Inizializzazione tick LVGL fallita");
        return false;
    }

    g_rotation_degree = abarth::settings::orientationToDegrees(abarth::settings::current().screen_orientation);
    ESP_LOGI(TAG, "Orientamento schermo impostato: %u gradi", g_rotation_degree);

    g_disp = display_init(lcd);
    if (!g_disp) {
        ESP_LOGE(TAG, "Inizializzazione display LVGL fallita");
        return false;
    }

    if (tp) {
        g_indev = indev_init(tp, g_disp);
        if (!g_indev) {
            ESP_LOGE(TAG, "Inizializzazione indev touch fallita");
            return false;
        }
    }

    BaseType_t core_id = (LVGL_PORT_TASK_CORE < 0) ? tskNO_AFFINITY : LVGL_PORT_TASK_CORE;
    BaseType_t ret = xTaskCreatePinnedToCore(
        lvgl_port_task, "lvgl", LVGL_PORT_TASK_STACK_SIZE, nullptr,
        LVGL_PORT_TASK_PRIORITY, &lvgl_task_handle, core_id
    );
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Creazione task LVGL fallita");
        return false;
    }

    ESP_LOGI(TAG, "LVGL 9 port inizializzato con successo");
    return true;
}

bool lvgl_port_deinit(void)
{
    if (lvgl_task_handle) {
        vTaskDelete(lvgl_task_handle);
        lvgl_task_handle = nullptr;
    }
    if (lvgl_tick_timer) {
        esp_timer_stop(lvgl_tick_timer);
        esp_timer_delete(lvgl_tick_timer);
        lvgl_tick_timer = nullptr;
    }
    if (lvgl_mux) {
        vSemaphoreDelete(lvgl_mux);
        lvgl_mux = nullptr;
    }
    if (g_slice_buf) {
        heap_caps_free(g_slice_buf);
        g_slice_buf = nullptr;
    }
    for (int i = 0; i < LVGL_PORT_BUFFER_NUM; i++) {
        if (lvgl_buf[i]) {
            heap_caps_free(lvgl_buf[i]);
            lvgl_buf[i] = nullptr;
        }
    }
    return true;
}

bool lvgl_port_lock(int timeout_ms)
{
    if (!lvgl_mux) return false;
    const TickType_t timeout_ticks = (timeout_ms < 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return (xSemaphoreTakeRecursive(lvgl_mux, timeout_ticks) == pdTRUE);
}

bool lvgl_port_unlock(void)
{
    if (!lvgl_mux) return false;
    return (xSemaphoreGiveRecursive(lvgl_mux) == pdTRUE);
}
