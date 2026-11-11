// Benchmark FPS della UI v2 (CONFIG_ABARTH_SCROLL_BENCH): per ogni pagina
// dati fa variare i valori a 20 Hz (come in auto) e conta i frame reali.
#include "sdkconfig.h"
#if CONFIG_ABARTH_SCROLL_BENCH
#include <cmath>
#include <esp_log.h>
#include <esp_timer.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "data/data_store.h"
#include "lvgl.h"
#include "display/lvgl_v8_port.h"
#include "ui_v2/ui_v2.h"
#include "ui_v2/page_setup.h"

namespace abarth::ui_v2::bench {
namespace {
constexpr const char* TAG = "BENCH_V2";

void feed(float t) {
    using data::Metric;
    auto& ds = data::store();
    const float s = 0.5f + 0.5f * std::sin(t * 1.3f);
    ds.set(Metric::Rpm, 800 + 6000 * s);
    ds.set(Metric::Speed, 30 + 120 * s);
    ds.set(Metric::BoostRelBar, -0.4f + 1.9f * s);
    ds.set(Metric::BoostKpa, 60 + 150 * s);
    ds.set(Metric::Throttle, 100 * s);
    ds.set(Metric::EngineLoad, 90 * s);
    ds.set(Metric::CoolantTempC, 80 + 15 * s);
    ds.set(Metric::OilTempC, 90 + 25 * s);
    ds.set(Metric::IntakeAirTempC, 25 + 20 * s);
    ds.set(Metric::AmbientTempC, 18);
    ds.set(Metric::EgtC, 400 + 450 * s);
    ds.set(Metric::BatteryVolts, 13.5f + 0.8f * s);
    ds.set(Metric::Afr, 12 + 3 * s);
    ds.set(Metric::FuelLevelPct, 58);
    ds.set(Metric::FuelRateLh, 2 + 15 * s);
    ds.set(Metric::FuelConsumptionL100, 5 + 20 * s);
    ds.set(Metric::FuelPressureKpa, 4000 + 3000 * s);
    ds.set(Metric::OilPressureBar, 1.5f + 3 * s);
}

int64_t s_r0 = 0, s_rsum = 0, s_w0 = 0, s_wsum = 0; uint32_t s_rn = 0;
void ev(lv_event_t* e) {
    const int64_t now = esp_timer_get_time();
    switch (lv_event_get_code(e)) {
        case LV_EVENT_REFR_START: s_r0 = now; break;
        case LV_EVENT_REFR_READY: if (s_r0) { s_rsum += now - s_r0; s_rn++; } break;
        case LV_EVENT_FLUSH_WAIT_START: s_w0 = now; break;
        case LV_EVENT_FLUSH_WAIT_FINISH: if (s_w0) s_wsum += now - s_w0; break;
        default: break;
    }
}

void task(void*) {
    vTaskDelay(pdMS_TO_TICKS(12000));
    const Page pages[] = {Page::Guida, Page::Temperature, Page::Pressioni, Page::Elettrico, Page::Personale};
    const char* names[] = {"GUIDA", "TEMPERATURE", "PRESSIONI", "ELETTRICO", "PERSONALE"};
    float t = 0;
    for (int i = 0; i < 5; ++i) {
        if (lvgl_port_lock(-1)) { showPage(pages[i]); lvgl_port_unlock(); }
        for (int k = 0; k < 20; ++k) { feed(t += 0.05f); vTaskDelay(pdMS_TO_TICKS(50)); }
        const uint32_t f0 = lvgl_port_get_frame_count();
        const int64_t t0 = esp_timer_get_time();
        for (int k = 0; k < 160; ++k) { feed(t += 0.05f); vTaskDelay(pdMS_TO_TICKS(50)); }  // 8 s
        const float fps = (lvgl_port_get_frame_count() - f0) * 1e6f / (esp_timer_get_time() - t0);
        ESP_LOGW(TAG, "pagina %s: %.1f fps con valori che cambiano a 20 Hz", names[i], fps);
    }

    // Cambio pagina: tempo per disegnare una pagina intera
    {
        if (lvgl_port_lock(-1)) {
            lv_display_t* d = lv_display_get_default();
            lv_display_add_event_cb(d, ev, LV_EVENT_REFR_START, nullptr);
            lv_display_add_event_cb(d, ev, LV_EVENT_REFR_READY, nullptr);
            lv_display_add_event_cb(d, ev, LV_EVENT_FLUSH_WAIT_START, nullptr);
            lv_display_add_event_cb(d, ev, LV_EVENT_FLUSH_WAIT_FINISH, nullptr);
            lvgl_port_unlock();
        }
        s_rsum = s_wsum = 0; s_rn = 0;
        const uint32_t f0 = lvgl_port_get_frame_count();
        const int64_t t0 = esp_timer_get_time();
        for (int k = 0; k < 30; ++k) {
            if (lvgl_port_lock(-1)) { showPage(pages[k % 5]); lvgl_port_unlock(); }
            // attende che il frame sia stato disegnato
            const uint32_t target = lvgl_port_get_frame_count() + 1;
            while (lvgl_port_get_frame_count() < target) vTaskDelay(1);
        }
        const int64_t dt = esp_timer_get_time() - t0;
        ESP_LOGW(TAG, "cambio pagina: %.1f ms per pagina intera (%.1f fps equivalenti, %lu frame)",
                 dt / 1000.0f / 30, 30 * 1e6f / dt, (unsigned long)(lvgl_port_get_frame_count() - f0));
        if (s_rn) ESP_LOGW(TAG, "  refresh medio %.1f ms, di cui attesa flush %.1f ms", s_rsum / 1000.0f / s_rn, s_wsum / 1000.0f / s_rn);
    }

    // Scroll continuo della lista Impostazioni
    if (lvgl_port_lock(-1)) { showPage(Page::Setup); lvgl_port_unlock(); }
    vTaskDelay(pdMS_TO_TICKS(500));
    lv_obj_t* list = setup_page::scrollTarget();
    if (list) {
        const uint32_t f0 = lvgl_port_get_frame_count();
        const int64_t t0 = esp_timer_get_time();
        bool down = true;
        for (int k = 0; k < 16; ++k) {
            if (lvgl_port_lock(-1)) {
                const int32_t max_y = lv_obj_get_scroll_bottom(list) + lv_obj_get_scroll_y(list);
                lv_obj_scroll_to_y(list, down ? max_y : 0, LV_ANIM_ON);
                lvgl_port_unlock();
            }
            down = !down;
            vTaskDelay(pdMS_TO_TICKS(500));
        }
        const float fps = (lvgl_port_get_frame_count() - f0) * 1e6f / (esp_timer_get_time() - t0);
        ESP_LOGW(TAG, "scroll Impostazioni: %.1f fps medi", fps);
    }
    if (lvgl_port_lock(-1)) { showPage(Page::Guida); lvgl_port_unlock(); }
    vTaskDelete(nullptr);
}
}  // namespace
void start() { xTaskCreate(task, "bench_v2", 4096, nullptr, 1, nullptr); }
}  // namespace abarth::ui_v2::bench
#else
namespace abarth::ui_v2::bench { void start() {} }
#endif
