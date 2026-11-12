// Cattura schermate di sviluppo della UI v2 (CONFIG_ABARTH_SCREENSHOT).
#include "sdkconfig.h"
#if CONFIG_ABARTH_SCREENSHOT
#include <cstdio>
#include <cstring>
#include <vector>
#include <esp_cache.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <mbedtls/base64.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "data/data_store.h"
#include "display/lvgl_v8_port.h"
#include "ui_v2/ui_v2.h"
#include "ui_v2/page_personale.h"
#include "ui_v2/settings_pages.h"

namespace abarth::display { void* debugFramebuffer(); }
namespace abarth::ui_v2 { Page debugCurrentPage(); }
extern "C" void lvgl_port_debug_touch(bool active, bool pressed, int32_t x, int32_t y);

namespace abarth::ui_v2::debug_shot {
namespace {
void fill_demo() {
    using data::Metric;
    auto& ds = data::store();
    ds.set(Metric::Rpm, 4870); ds.set(Metric::Speed, 87); ds.set(Metric::BoostRelBar, 1.24f);
    ds.set(Metric::BoostKpa, 183); ds.set(Metric::CoolantTempC, 88); ds.set(Metric::OilTempC, 104);
    ds.set(Metric::IntakeAirTempC, 34); ds.set(Metric::AmbientTempC, 18); ds.set(Metric::EgtC, 712);
    ds.set(Metric::BatteryVolts, 14.1f); ds.set(Metric::Afr, 13.2f); ds.set(Metric::Throttle, 64);
    ds.set(Metric::EngineLoad, 71); ds.set(Metric::FuelLevelPct, 58); ds.set(Metric::FuelRateLh, 9.4f);
    ds.set(Metric::FuelConsumptionL100, 10.8f); ds.set(Metric::FuelPressureKpa, 5400);
    ds.set(Metric::OilPressureBar, 3.6f); ds.set(Metric::FuelTempC, 41); ds.set(Metric::FuelInjectionTiming, 11.5f);
    ds.set(Metric::EthanolPct, 0); ds.set(Metric::DistanceSinceClear, 1234);
}

void dump(int idx) {
    auto* fb = static_cast<uint16_t*>(display::debugFramebuffer());
    if (!fb) return;
    const size_t n = 1024 * 600;
    esp_cache_msync(fb, n * 2, ESP_CACHE_MSYNC_FLAG_DIR_M2C);
    // RLE: coppie (conteggio u16, pixel u16)
    std::vector<uint16_t> rle;
    rle.reserve(64 * 1024);
    size_t i = 0;
    while (i < n) {
        uint16_t px = fb[i]; uint16_t cnt = 1;
        while (i + cnt < n && cnt < 65535 && fb[i + cnt] == px) ++cnt;
        rle.push_back(cnt); rle.push_back(px);
        i += cnt;
    }
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(rle.data());
    const size_t total = rle.size() * 2;
    esp_log_level_set("*", ESP_LOG_NONE);
    printf("\n@SHOT_BEGIN %d %u\n", idx, (unsigned)total);
    constexpr size_t kChunk = 600;
    unsigned char out[kChunk * 4 / 3 + 8];
    size_t line = 0;
    for (size_t off = 0; off < total; off += kChunk, ++line) {
        const size_t len = (total - off) < kChunk ? (total - off) : kChunk;
        size_t olen = 0;
        mbedtls_base64_encode(out, sizeof(out), &olen, bytes + off, len);
        out[olen] = 0;
        printf("@S %u %s\n", (unsigned)line, out);
        if ((line & 15) == 0) vTaskDelay(1);
    }
    printf("@SHOT_END %d\n", idx);
    fflush(stdout);
    esp_log_level_set("*", ESP_LOG_INFO);
}

// Trascina da (x,y0) a (x,y1) in `ms` millisecondi con il tocco simulato
void drag(int32_t x, int32_t y0, int32_t y1, int ms) {
    const int steps = ms / 20;
    for (int i = 0; i <= steps; ++i) {
        lvgl_port_debug_touch(true, true, x, y0 + (y1 - y0) * i / steps);
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    lvgl_port_debug_touch(true, false, x, y1);
    vTaskDelay(pdMS_TO_TICKS(100));
    lvgl_port_debug_touch(false, false, 0, 0);
}
void tap(int32_t x, int32_t y) {
    lvgl_port_debug_touch(true, true, x, y);
    vTaskDelay(pdMS_TO_TICKS(120));
    lvgl_port_debug_touch(true, false, x, y);
    vTaskDelay(pdMS_TO_TICKS(100));
    lvgl_port_debug_touch(false, false, 0, 0);
}

void touch_tests() {
    // Impostazioni: apri ogni categoria toccando la sua riga, trascina in su,
    // verifica che la sottopagina scorra e che non si apra un'altra categoria.
    if (lvgl_port_lock(-1)) { settings_pages::debugOpen(-1); showPage(Page::Setup); lvgl_port_unlock(); }
    vTaskDelay(pdMS_TO_TICKS(400));
    for (int n = 0; n < 11; ++n) {
        // righe dell'elenco: inizio scheda y=56+10+34, righe 72 px + 1 di separatore
        const int32_t y = 56 + 8 + 34 + n * 73 + 36;
        tap(300, y);
        vTaskDelay(pdMS_TO_TICKS(400));
        int depth0 = 0; int32_t sy0 = 0, sy1 = 0, bottom = 0;
        if (lvgl_port_lock(-1)) { depth0 = settings_pages::debugDepth(); lv_obj_t* l = settings_pages::debugTopList(); sy0 = l ? lv_obj_get_scroll_y(l) : -1; bottom = l ? lv_obj_get_scroll_bottom(l) : 0; lvgl_port_unlock(); }
        drag(300, 850, 350, 500);
        vTaskDelay(pdMS_TO_TICKS(600));
        int depth1 = 0;
        if (lvgl_port_lock(-1)) { depth1 = settings_pages::debugDepth(); lv_obj_t* l = settings_pages::debugTopList(); sy1 = l ? lv_obj_get_scroll_y(l) : -1; lvgl_port_unlock(); }
        const bool ok = depth0 == 1 && depth1 == 1 && (bottom <= 0 || sy1 > sy0);
        ESP_LOGW("TOUCH_TEST", "categoria %d: aperta=%d dopo_drag=%d scroll %ld -> %ld (scorribile %ld) %s",
                 n, depth0, depth1, (long)sy0, (long)sy1, (long)bottom, ok ? "OK" : "ERRORE");
        if (lvgl_port_lock(-1)) { settings_pages::debugOpen(-1); lvgl_port_unlock(); }
        vTaskDelay(pdMS_TO_TICKS(300));
    }
    // swipe orizzontale dentro una sottopagina: non deve cambiare pagina
    tap(300, 56 + 8 + 34 + 36);
    vTaskDelay(pdMS_TO_TICKS(400));
    for (int i = 0; i <= 15; ++i) { lvgl_port_debug_touch(true, true, 500 - i * 25, 600); vTaskDelay(pdMS_TO_TICKS(20)); }
    lvgl_port_debug_touch(true, false, 125, 600); vTaskDelay(pdMS_TO_TICKS(100)); lvgl_port_debug_touch(false, false, 0, 0);
    vTaskDelay(pdMS_TO_TICKS(400));
    int d = 0;
    if (lvgl_port_lock(-1)) { d = settings_pages::debugDepth(); lvgl_port_unlock(); }
    ESP_LOGW("TOUCH_TEST", "swipe orizzontale in sottopagina: profondita' %d (atteso 1) %s", d, d == 1 ? "OK" : "ERRORE");
    if (lvgl_port_lock(-1)) { settings_pages::debugOpen(-1); showPage(Page::Guida); lvgl_port_unlock(); }
}

void gear_test() {
    // da ogni pagina: ingranaggio -> IMPOSTAZIONI, di nuovo -> pagina di partenza
    const int32_t gx = LV_HOR_RES - 12 - 36, gy = 1000;  // ingranaggio in basso a destra
    int ok = 0;
    for (int p = 0; p < static_cast<int>(Page::Setup); ++p) {
        if (lvgl_port_lock(-1)) { showPage(static_cast<Page>(p)); lvgl_port_unlock(); }
        vTaskDelay(pdMS_TO_TICKS(300));
        tap(gx, gy);
        vTaskDelay(pdMS_TO_TICKS(300));
        const Page a = debugCurrentPage();
        tap(gx, gy);
        vTaskDelay(pdMS_TO_TICKS(300));
        const Page b = debugCurrentPage();
        const bool good = a == Page::Setup && static_cast<int>(b) == p;
        ok += good;
        ESP_LOGW("TOUCH_TEST", "ingranaggio da pagina %d: -> %d -> %d %s", p, (int)a, (int)b, good ? "OK" : "ERRORE");
    }
    // da una sottopagina delle impostazioni: torna alla pagina di prima e chiude la sottopagina
    if (lvgl_port_lock(-1)) { showPage(Page::Guida); lvgl_port_unlock(); }
    tap(gx, gy);
    vTaskDelay(pdMS_TO_TICKS(300));
    tap(300, 134);   // "Collegamento"
    vTaskDelay(pdMS_TO_TICKS(400));
    tap(gx, gy);
    vTaskDelay(pdMS_TO_TICKS(400));
    ESP_LOGW("TOUCH_TEST", "ingranaggio da sottopagina: pagina %d (atteso 0), sottopagine %d (atteso 0) -> %d/5 pagine OK",
             (int)debugCurrentPage(), settings_pages::debugDepth(), ok);
}

void task(void*) {
    vTaskDelay(pdMS_TO_TICKS(15000));
    gear_test();
    if (lvgl_port_lock(-1)) { showPage(Page::Guida); lvgl_port_unlock(); }
    vTaskDelay(pdMS_TO_TICKS(500));
    dump(0);
    touch_tests();
    // Impostazioni: elenco categorie e ogni sottopagina (in alto e in fondo)
    if (lvgl_port_lock(-1)) { showPage(Page::Setup); lvgl_port_unlock(); }
    vTaskDelay(pdMS_TO_TICKS(500));
    dump(30);
    for (int n = 0; n < 11; ++n) {
        if (lvgl_port_lock(-1)) { settings_pages::debugOpen(n); lvgl_port_unlock(); }
        for (int k = 0; k < 8; ++k) { fill_demo(); vTaskDelay(pdMS_TO_TICKS(100)); }
        dump(40 + n * 2);
        bool more = false;
        if (lvgl_port_lock(-1)) { more = settings_pages::debugScrollBottom(); lvgl_port_unlock(); }
        if (more) { vTaskDelay(pdMS_TO_TICKS(500)); dump(41 + n * 2); }
    }
    if (lvgl_port_lock(-1)) { settings_pages::debugOpen(-1); showPage(Page::Guida); lvgl_port_unlock(); }
    vTaskDelete(nullptr);
}
}  // namespace

void start() { xTaskCreate(task, "shot", 8192, nullptr, 1, nullptr); }
}  // namespace abarth::ui_v2::debug_shot
#else
namespace abarth::ui_v2::debug_shot { void start() {} }
#endif
