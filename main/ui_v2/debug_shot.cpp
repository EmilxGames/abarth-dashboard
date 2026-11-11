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

namespace abarth::display { void* debugFramebuffer(); }

namespace abarth::ui_v2::debug_shot {
namespace {
void fill_demo() {
    using data::Metric;
    auto& ds = data::store();
    ds.set(Metric::Rpm, 4870); ds.set(Metric::Speed, 87); ds.set(Metric::BoostRelBar, 0.82f);
    ds.set(Metric::BoostKpa, 183); ds.set(Metric::CoolantTempC, 91); ds.set(Metric::OilTempC, 104);
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

void task(void*) {
    vTaskDelay(pdMS_TO_TICKS(15000));
    for (int p = 0; p < static_cast<int>(Page::Count); ++p) {
        for (int k = 0; k < 10; ++k) { fill_demo(); vTaskDelay(pdMS_TO_TICKS(100)); }
        if (lvgl_port_lock(-1)) { showPage(static_cast<Page>(p)); lvgl_port_unlock(); }
        for (int k = 0; k < 10; ++k) { fill_demo(); vTaskDelay(pdMS_TO_TICKS(100)); }
        dump(p);
    }
    vTaskDelete(nullptr);
}
}  // namespace

void start() { xTaskCreate(task, "shot", 8192, nullptr, 1, nullptr); }
}  // namespace abarth::ui_v2::debug_shot
#else
namespace abarth::ui_v2::debug_shot { void start() {} }
#endif
