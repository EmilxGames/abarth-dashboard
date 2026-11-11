#include "storage/telemetry_logger.h"

#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <esp_log.h>
#include <esp_timer.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "config/warmup_manager.h"
#include "data/data_store.h"
#include "storage/sd_card_manager.h"

namespace abarth::storage::telemetry {

namespace {
constexpr const char* TAG = "telemetry";
constexpr uint32_t kPeriodMs     = 1000;   ///< 1 riga al secondo
constexpr int64_t  kSyncPeriodMs = 5000;   ///< fflush + fsync ogni 5 s
constexpr uint32_t kFreshMs      = 2000;   ///< oltre questa eta' il valore e' vuoto
constexpr size_t   kBufSize      = 4096;

SemaphoreHandle_t g_mtx = nullptr;
TaskHandle_t g_task = nullptr;
FILE* g_file = nullptr;
char g_path[128] = {};
bool g_paused = false;
char* g_vbuf = nullptr;

void close_file_locked() {
    if (g_file) {
        fflush(g_file);
        fsync(fileno(g_file));
        fclose(g_file);
        g_file = nullptr;
        ESP_LOGI(TAG, "file log chiuso: %s", g_path);
    }
    g_path[0] = '\0';
}

bool ensure_open_locked() {
    auto& sd = storage::sd();
    if (!sd.isMounted()) {
        close_file_locked();
        return false;
    }
    const char* active = sd.getActiveLogPath();
    if (!active || active[0] == '\0') return false;
    if (g_file && std::strcmp(active, g_path) == 0) return true;

    close_file_locked();  // il percorso e' cambiato (nuova sessione)
    g_file = fopen(active, "a");
    if (!g_file) {
        ESP_LOGW(TAG, "impossibile aprire %s", active);
        return false;
    }
    if (!g_vbuf) g_vbuf = static_cast<char*>(malloc(kBufSize));
    if (g_vbuf) setvbuf(g_file, g_vbuf, _IOFBF, kBufSize);
    std::snprintf(g_path, sizeof(g_path), "%s", active);
    ESP_LOGI(TAG, "registrazione telemetria su %s", g_path);
    return true;
}

/// Aggiunge "valore," se fresco, altrimenti "," (campo vuoto). Aggiorna l'eta' massima.
int append_value(char* out, size_t cap, const data::Sample& s, int64_t now_ms, const char* fmt,
                 int64_t& age_max) {
    if (!s.fresh(kFreshMs, now_ms)) return std::snprintf(out, cap, ",");
    const int64_t age = now_ms - s.updated_ms;
    if (age > age_max) age_max = age;
    char v[24];
    std::snprintf(v, sizeof(v), fmt, s.value);
    return std::snprintf(out, cap, "%s,", v);
}

void logger_task(void*) {
    int64_t last_sync_ms = 0;
    TickType_t wake = xTaskGetTickCount();
    while (true) {
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(kPeriodMs));
        const int64_t now_ms = esp_timer_get_time() / 1000;
        auto& ds = data::store();
        const data::Sample rpm = ds.get(data::Metric::Rpm);
        const data::Sample spd = ds.get(data::Metric::Speed);

        // Registra solo se una sorgente (CAN o OBD) sta mandando dati
        const bool active = rpm.fresh(kFreshMs, now_ms) || spd.fresh(kFreshMs, now_ms);

        xSemaphoreTake(g_mtx, portMAX_DELAY);
        if (!g_paused && active && ensure_open_locked()) {
            char line[200];
            size_t n = std::snprintf(line, sizeof(line), "%lld,", static_cast<long long>(now_ms));
            int64_t age_max = 0;
            n += append_value(line + n, sizeof(line) - n, rpm, now_ms, "%.0f", age_max);
            n += append_value(line + n, sizeof(line) - n, spd, now_ms, "%.1f", age_max);
            n += append_value(line + n, sizeof(line) - n, ds.get(data::Metric::BoostRelBar), now_ms, "%.2f", age_max);
            n += append_value(line + n, sizeof(line) - n, ds.get(data::Metric::CoolantTempC), now_ms, "%.1f", age_max);
            n += std::snprintf(line + n, sizeof(line) - n, "%.1f,", abarth::warmup::warmup_mgr().estimatedOilTemp());
            n += append_value(line + n, sizeof(line) - n, ds.get(data::Metric::IntakeAirTempC), now_ms, "%.1f", age_max);
            n += append_value(line + n, sizeof(line) - n, ds.get(data::Metric::Throttle), now_ms, "%.1f", age_max);
            if (n < sizeof(line)) {
                std::snprintf(line + n, sizeof(line) - n, "%lld\n", static_cast<long long>(age_max));
            }
            if (fputs(line, g_file) < 0) {
                ESP_LOGW(TAG, "errore di scrittura: chiudo il file");
                close_file_locked();
            } else if (now_ms - last_sync_ms >= kSyncPeriodMs) {
                last_sync_ms = now_ms;
                fflush(g_file);
                fsync(fileno(g_file));
            }
        } else if (!active && g_file && now_ms - last_sync_ms >= kSyncPeriodMs) {
            // Nessun dato: svuota il buffer cosi' il file e' completo se si spegne
            last_sync_ms = now_ms;
            fflush(g_file);
            fsync(fileno(g_file));
        }
        xSemaphoreGive(g_mtx);
    }
}
}  // namespace

void start() {
    if (g_task) return;
    if (!g_mtx) g_mtx = xSemaphoreCreateMutex();
    if (!g_mtx) return;
    xTaskCreatePinnedToCore(logger_task, "telemetry", 4096, nullptr, 2, &g_task, 0);
}

void pause() {
    if (!g_mtx) return;
    xSemaphoreTake(g_mtx, portMAX_DELAY);
    g_paused = true;
    close_file_locked();
    xSemaphoreGive(g_mtx);
}

void resume() {
    if (!g_mtx) return;
    xSemaphoreTake(g_mtx, portMAX_DELAY);
    g_paused = false;
    xSemaphoreGive(g_mtx);
}

}  // namespace abarth::storage::telemetry
