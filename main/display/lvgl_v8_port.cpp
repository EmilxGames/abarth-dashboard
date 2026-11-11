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
#include <esp_cache.h>
#include <driver/ppa.h>
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
static volatile uint32_t g_frame_count = 0;

// Pannello fisico (landscape nativo)
static constexpr int kPhysW = 1024;
static constexpr int kPhysH = 600;
static constexpr size_t kCacheLine = 64;  // CONFIG_CACHE_L1/L2_CACHE_LINE_SIZE

// Rotazione hardware con PPA (Scale-Rotate-Mirror): LVGL -> framebuffer DPI
static ppa_client_handle_t s_ppa = nullptr;
static SemaphoreHandle_t s_ppa_done = nullptr;
static uint16_t *s_fb = nullptr;
static size_t s_fb_size = 0;
static bool s_use_ppa = false;
static ppa_srm_rotation_angle_t s_ppa_angle = PPA_SRM_ROTATION_ANGLE_0;

// Fast L1 SRAM scratchpad for tiled rotation (4 KB total, static in internal DRAM BSS)
alignas(16) static uint16_t s_tile_in[32][32];
alignas(16) static uint16_t s_tile_out[32][32];

// Rotazione software (fallback se la PPA non e' disponibile).
// Cache-blocked 32x32 con letture/scritture PSRAM sequenziali.
static void rotate_slice_16bpp(
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

// Fallback senza PPA: un solo tentativo con attesa bloccante sul semaforo del
// driver (niente busy-wait con esp_rom_delay_us).
static void safe_draw_bitmap(LCD *lcd, int x, int y, int w, int h, const uint8_t *data)
{
    if (!lcd->drawBitmap(x, y, w, h, data, 100)) {
        ESP_LOGW(TAG, "drawBitmap fallito (%d,%d %dx%d)", x, y, w, h);
    }
}

// ISR PPA: transazione finita -> sblocca flush_wait_cb
static bool IRAM_ATTR ppa_trans_done_cb(ppa_client_handle_t, ppa_event_data_t *, void *)
{
    BaseType_t hp = pdFALSE;
    xSemaphoreGiveFromISR(s_ppa_done, &hp);
    return hp == pdTRUE;
}

// Chiamata da LVGL quando deve riusare un buffer ancora in flush
static void flush_wait_cb(lv_display_t *disp)
{
    if (xSemaphoreTake(s_ppa_done, pdMS_TO_TICKS(200)) != pdTRUE) {
        ESP_LOGW(TAG, "timeout attesa PPA");
    }
}

// Coordinate fisiche del blocco ruotato (stessa convenzione della rotazione CPU)
static void logical_to_phys(int x1, int y1, int x2, int y2, int &px, int &py, int &pw, int &ph)
{
    const int w = x2 - x1 + 1;
    const int h = y2 - y1 + 1;
    if (g_rotation_degree == 90) {
        px = kPhysW - 1 - y2; py = x1; pw = h; ph = w;
    } else if (g_rotation_degree == 270) {
        px = y1; py = kPhysH - 1 - x2; pw = h; ph = w;
    } else {
        px = x1; py = y1; pw = w; ph = h;
    }
}

static esp_err_t ppa_blit(const uint16_t *src, int w, int h, void *dst, size_t dst_size,
                          int dst_w, int dst_h, int off_x, int off_y,
                          ppa_srm_rotation_angle_t angle, ppa_trans_mode_t mode)
{
    ppa_srm_oper_config_t op = {};
    op.in.buffer = src;
    op.in.pic_w = w;
    op.in.pic_h = h;
    op.in.block_w = w;
    op.in.block_h = h;
    op.in.block_offset_x = 0;
    op.in.block_offset_y = 0;
    op.in.srm_cm = PPA_SRM_COLOR_MODE_RGB565;
    op.out.buffer = dst;
    op.out.buffer_size = dst_size;
    op.out.pic_w = dst_w;
    op.out.pic_h = dst_h;
    op.out.block_offset_x = off_x;
    op.out.block_offset_y = off_y;
    op.out.srm_cm = PPA_SRM_COLOR_MODE_RGB565;
    op.rotation_angle = angle;
    op.scale_x = 1.0f;
    op.scale_y = 1.0f;
    op.mirror_x = false;
    op.mirror_y = false;
    op.rgb_swap = false;
    op.byte_swap = false;
    op.alpha_update_mode = PPA_ALPHA_NO_CHANGE;
    op.mode = mode;
    op.user_data = nullptr;
    return ppa_do_scale_rotate_mirror(s_ppa, &op);
}

// Verifica all'avvio che la PPA ruoti come la funzione software: confronta un
// piccolo pattern. Se l'angolo atteso non coincide prova quello opposto.
static bool ppa_self_test(void)
{
    if (g_rotation_degree == 0) {
        s_ppa_angle = PPA_SRM_ROTATION_ANGLE_0;
        return true;
    }
    constexpr int TW = 40, TH = 24;
    const size_t out_bytes = (TW * TH * sizeof(uint16_t) + kCacheLine - 1) & ~(kCacheLine - 1);
    uint16_t *in = (uint16_t *)heap_caps_aligned_alloc(kCacheLine, TW * TH * 2, MALLOC_CAP_SPIRAM);
    uint16_t *ref = (uint16_t *)heap_caps_malloc(TW * TH * 2, MALLOC_CAP_SPIRAM);
    uint16_t *out = (uint16_t *)heap_caps_aligned_alloc(kCacheLine, out_bytes, MALLOC_CAP_SPIRAM);
    bool ok = false;
    if (in && ref && out) {
        for (int i = 0; i < TW * TH; i++) in[i] = (uint16_t)(i * 2654435761u >> 16);
        rotate_slice_16bpp(in, ref, TW, TH, g_rotation_degree);
        const ppa_srm_rotation_angle_t first = (g_rotation_degree == 90) ? PPA_SRM_ROTATION_ANGLE_270
                                                                         : PPA_SRM_ROTATION_ANGLE_90;
        const ppa_srm_rotation_angle_t second = (first == PPA_SRM_ROTATION_ANGLE_270) ? PPA_SRM_ROTATION_ANGLE_90
                                                                                       : PPA_SRM_ROTATION_ANGLE_270;
        for (ppa_srm_rotation_angle_t a : {first, second}) {
            memset(out, 0, out_bytes);
            esp_cache_msync(out, out_bytes, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
            if (ppa_blit(in, TW, TH, out, out_bytes, TH, TW, 0, 0, a, PPA_TRANS_MODE_BLOCKING) != ESP_OK) break;
            if (memcmp(out, ref, TW * TH * 2) == 0) {
                s_ppa_angle = a;
                ok = true;
                if (a != first) ESP_LOGW(TAG, "PPA: verso di rotazione invertito rispetto all'atteso, corretto");
                break;
            }
        }
    }
    heap_caps_free(in);
    heap_caps_free(ref);
    heap_caps_free(out);
    return ok;
}

static void ppa_init(LCD *lcd)
{
    s_use_ppa = false;
    s_fb = (uint16_t *)lcd->getFrameBufferByIndex(0);
    s_fb_size = (size_t)kPhysW * kPhysH * sizeof(uint16_t);
    if (!s_fb || ((uintptr_t)s_fb & (kCacheLine - 1)) || (s_fb_size & (kCacheLine - 1))) {
        ESP_LOGW(TAG, "PPA: framebuffer non disponibile o non allineato, uso la rotazione software");
        return;
    }
    s_ppa_done = xSemaphoreCreateBinary();
    ppa_client_config_t cfg = {};
    cfg.oper_type = PPA_OPERATION_SRM;
    cfg.max_pending_trans_num = 1;
    if (!s_ppa_done || ppa_register_client(&cfg, &s_ppa) != ESP_OK) {
        ESP_LOGW(TAG, "PPA: registrazione client fallita, uso la rotazione software");
        return;
    }
    if (!ppa_self_test()) {
        ESP_LOGE(TAG, "PPA: self-test rotazione fallito, uso la rotazione software");
        ppa_unregister_client(s_ppa);
        s_ppa = nullptr;
        return;
    }
    ppa_event_callbacks_t cbs = {};
    cbs.on_trans_done = ppa_trans_done_cb;
    ppa_client_register_event_callbacks(s_ppa, &cbs);
    // La CPU non scrive piu' nel framebuffer: pulisci eventuali linee sporche
    esp_cache_msync(s_fb, s_fb_size, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
    s_use_ppa = true;
    ESP_LOGI(TAG, "PPA attiva: rotazione %u gradi in hardware, flush asincrono", g_rotation_degree);
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

    if (lv_display_flush_is_last(disp)) g_frame_count++;

    int phys_x, phys_y, phys_w, phys_h;
    logical_to_phys(x1, y1, x2, y2, phys_x, phys_y, phys_w, phys_h);
    const bool in_bounds = phys_x >= 0 && phys_y >= 0 && phys_w > 0 && phys_h > 0 &&
                           phys_x + phys_w <= kPhysW && phys_y + phys_h <= kPhysH;

    if (s_use_ppa && in_bounds) {
        // Asincrono: LVGL continua a disegnare nell'altro buffer; flush_wait_cb
        // aspetta la fine della PPA prima di riusare questo.
        if (ppa_blit((const uint16_t *)px_map, w, h, s_fb, s_fb_size, kPhysW, kPhysH,
                     phys_x, phys_y, s_ppa_angle, PPA_TRANS_MODE_NON_BLOCKING) == ESP_OK) {
            return;  // lv_display_flush_ready implicito in flush_wait_cb
        }
        ESP_LOGW(TAG, "PPA fallita, fallback software per questo flush");
    }

    if (g_rotation_degree != 0 && in_bounds) {
        if (!g_slice_buf) {
            g_slice_buf = (uint16_t *)heap_caps_aligned_alloc(kCacheLine, (size_t)kPhysW * kPhysH * sizeof(uint16_t),
                                                              MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        }
        if (g_slice_buf) {
            rotate_slice_16bpp((const uint16_t *)px_map, g_slice_buf, w, h, g_rotation_degree);
            safe_draw_bitmap(lcd, phys_x, phys_y, phys_w, phys_h, (const uint8_t *)g_slice_buf);
        }
    } else if (g_rotation_degree == 0) {
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
        // Allineati alla linea di cache (64 B) per DMA/PPA
        lvgl_buf[i] = heap_caps_aligned_alloc(kCacheLine, buffer_bytes, LVGL_PORT_BUFFER_MALLOC_CAPS);
        if (!lvgl_buf[i]) {
            ESP_LOGE(TAG, "Allocazione buffer LVGL %d fallita (%u bytes)", i, (unsigned)buffer_bytes);
            return nullptr;
        }
        ESP_LOGI(TAG, "Buffer LVGL %d allocato: %p (%u bytes)", i, lvgl_buf[i], (unsigned)buffer_bytes);
    }

    lv_display_set_buffers(disp, lvgl_buf[0], lvgl_buf[1], buffer_bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(disp, flush_callback);

    ppa_init(lcd);
    if (s_use_ppa) {
        lv_display_set_flush_wait_cb(disp, flush_wait_cb);
    }

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

uint32_t lvgl_port_get_frame_count(void)
{
    return g_frame_count;
}
