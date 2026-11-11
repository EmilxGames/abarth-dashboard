#include <cstdint>
#include "ui/scroll_bench.h"
#include "sdkconfig.h"

#if CONFIG_ABARTH_SCROLL_BENCH
#include <esp_log.h>
#include <esp_timer.h>
#include <esp_heap_caps.h>
#include "lvgl.h"
#include "display/lvgl_v8_port.h"

namespace abarth::ui::scroll_bench {
namespace {
constexpr const char* TAG = "BENCH";
lv_obj_t* s_tv = nullptr;
lv_obj_t* s_target = nullptr;
uint32_t s_tab = 0;
int s_step = 0;
bool s_down = true;
uint32_t s_last_frames = 0;
int64_t s_last_us = 0;
uint32_t s_start_frames = 0;
int64_t s_start_us = 0;
float s_min_fps = 1000.0f;
int64_t s_refr_t0 = 0, s_refr_sum = 0, s_wait_t0 = 0, s_wait_sum = 0;
uint32_t s_refr_n = 0;

void disp_ev(lv_event_t* e) {
    const int64_t now = esp_timer_get_time();
    switch (lv_event_get_code(e)) {
        case LV_EVENT_REFR_START: s_refr_t0 = now; break;
        case LV_EVENT_REFR_READY: if (s_refr_t0) { s_refr_sum += now - s_refr_t0; s_refr_n++; } break;
        case LV_EVENT_FLUSH_WAIT_START: s_wait_t0 = now; break;
        case LV_EVENT_FLUSH_WAIT_FINISH: if (s_wait_t0) s_wait_sum += now - s_wait_t0; break;
        default: break;
    }
}

void tick_cb(lv_timer_t* t) {
    ++s_step;
    if (s_step == 1) {
        lv_tabview_set_act(s_tv, s_tab, LV_ANIM_OFF);
        return;
    }
    if (s_step == 3) {
        s_start_frames = s_last_frames = lvgl_port_get_frame_count();
        s_start_us = s_last_us = esp_timer_get_time();
        lv_display_t* d = lv_display_get_default();
        lv_display_add_event_cb(d, disp_ev, LV_EVENT_REFR_START, nullptr);
        lv_display_add_event_cb(d, disp_ev, LV_EVENT_REFR_READY, nullptr);
        lv_display_add_event_cb(d, disp_ev, LV_EVENT_FLUSH_WAIT_START, nullptr);
        lv_display_add_event_cb(d, disp_ev, LV_EVENT_FLUSH_WAIT_FINISH, nullptr);
        ESP_LOGI(TAG, "Inizio scroll benchmark (heap interna libera %u B, blocco max %u B)",
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA));
    }
    if (s_step < 3) return;

    // Scroll alternato su e giu' (animato): ogni passo 500 ms
    const int32_t max_y = lv_obj_get_scroll_bottom(s_target) + lv_obj_get_scroll_y(s_target);
    lv_obj_scroll_to_y(s_target, s_down ? max_y : 0, LV_ANIM_ON);
    s_down = !s_down;

    const uint32_t frames = lvgl_port_get_frame_count();
    const int64_t now = esp_timer_get_time();
    const float fps = (frames - s_last_frames) * 1e6f / (float)(now - s_last_us);
    if (s_step > 3 && fps < s_min_fps) s_min_fps = fps;
    if (s_step > 3) ESP_LOGI(TAG, "fps=%.1f (scroll max_y=%ld)", fps, (long)max_y);
    s_last_frames = frames;
    s_last_us = now;

    if (s_step >= 3 + 40) {  // 40 passi da 500 ms = 20 s
        const float avg = (frames - s_start_frames) * 1e6f / (float)(now - s_start_us);
        ESP_LOGW(TAG, "RISULTATO scroll Impostazioni: media %.1f fps, minimo %.1f fps (%lu frame in %.1f s)",
                 avg, s_min_fps, (unsigned long)(frames - s_start_frames), (now - s_start_us) / 1e6f);
        if (s_refr_n) {
            ESP_LOGW(TAG, "Tempo medio per refresh: %.1f ms, di cui attesa PPA/flush %.1f ms",
                     s_refr_sum / 1000.0f / s_refr_n, s_wait_sum / 1000.0f / s_refr_n);
        }
        lv_timer_delete(t);
    }
}
}  // namespace

void start(lv_obj_t* tabview, uint32_t settings_tab_idx, lv_obj_t* scroll_target) {
    if (!tabview || !scroll_target) return;
    s_tv = tabview;
    s_tab = settings_tab_idx;
    s_target = scroll_target;
    lv_timer_t* t = lv_timer_create(tick_cb, 500, nullptr);
    lv_timer_set_repeat_count(t, -1);
    // primo passo dopo 8 s per far finire splash e inizializzazioni
    lv_timer_set_period(t, 500);
    lv_timer_pause(t);
    lv_timer_t* d = lv_timer_create([](lv_timer_t* dt) {
        lv_timer_resume((lv_timer_t*)lv_timer_get_user_data(dt));
        lv_timer_delete(dt);
    }, 8000, t);
    (void)d;
    ESP_LOGW(TAG, "Scroll benchmark attivo: parte tra 8 s");
}
}  // namespace abarth::ui::scroll_bench

#else
namespace abarth::ui::scroll_bench {
void start(lv_obj_t*, uint32_t, lv_obj_t*) {}
}
#endif
