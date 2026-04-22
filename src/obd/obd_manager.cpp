#include "obd/obd_manager.h"

#include <esp_log.h>

#include <cstring>

#include "config/app_config.h"
#include "data/data_store.h"
#include "obd/pids.h"

namespace abarth::obd {

namespace {
constexpr const char* TAG = "obd_mgr";
ObdManager g_instance;
}  // namespace

ObdManager& obd() { return g_instance; }

const char* stateName(ObdState s) {
    switch (s) {
        case ObdState::Disconnected: return "Disconnesso";
        case ObdState::Scanning:     return "Ricerca dongle...";
        case ObdState::Connecting:   return "Connessione...";
        case ObdState::Initializing: return "Inizializzazione ELM...";
        case ObdState::Polling:      return "Online";
        case ObdState::Error:        return "Errore";
    }
    return "?";
}

ObdManager::ObdManager() : elm_(ble_) {}

bool ObdManager::start() {
    if (task_) return true;
    BaseType_t ok = xTaskCreatePinnedToCore(
        &ObdManager::taskTrampoline, "obd", 8192, this, 4, &task_, 1);
    return ok == pdPASS;
}

void ObdManager::setState(ObdState s) {
    if (state_.load() == s) return;
    ESP_LOGI(TAG, "stato: %s -> %s", stateName(state_.load()), stateName(s));
    state_.store(s);
}

void ObdManager::taskTrampoline(void* arg) {
    static_cast<ObdManager*>(arg)->run();
}

void ObdManager::run() {
    using namespace abarth::config;

    BleStreamConfig cfg{};
    cfg.device_name  = kDefaultBleDeviceName;
    cfg.service_uuid = kBleServiceUUID;
    cfg.notify_uuid  = kBleNotifyCharUUID;
    cfg.write_uuid   = kBleWriteCharUUID;
    cfg.scan_time_s  = 10;
    if (!ble_.begin(cfg)) {
        last_error_ = "BLE begin fallito";
        setState(ObdState::Error);
        vTaskDelete(nullptr);
        return;
    }

    while (true) {
        switch (state_.load()) {
            case ObdState::Disconnected:
            case ObdState::Error: {
                setState(ObdState::Scanning);
                break;
            }
            case ObdState::Scanning: {
                if (ble_.connect()) {
                    setState(ObdState::Initializing);
                } else {
                    last_error_ = "dongle non trovato / non connesso";
                    setState(ObdState::Disconnected);
                    vTaskDelay(pdMS_TO_TICKS(3000));
                }
                break;
            }
            case ObdState::Initializing: {
                if (elm_.init(2500)) {
                    setState(ObdState::Polling);
                } else {
                    last_error_ = "init ELM327 fallita";
                    ble_.disconnect();
                    setState(ObdState::Disconnected);
                    vTaskDelay(pdMS_TO_TICKS(2000));
                }
                break;
            }
            case ObdState::Polling: {
                if (!pollOnce()) {
                    last_error_ = "connessione persa";
                    ble_.disconnect();
                    setState(ObdState::Disconnected);
                    vTaskDelay(pdMS_TO_TICKS(1000));
                }
                vTaskDelay(pdMS_TO_TICKS(kPollIntervalMs));
                break;
            }
            case ObdState::Connecting: {
                // transizione interna non usata
                setState(ObdState::Initializing);
                break;
            }
        }
    }
}

bool ObdManager::pollOnce() {
    using namespace abarth::config;

    auto try_pid = [&](const PidSpec& spec, int64_t& last_call_ms) -> bool {
        const int64_t now_ms = esp_timer_get_time() / 1000;
        if (spec.period_ms > 0 && (now_ms - last_call_ms) < static_cast<int64_t>(spec.period_ms)) {
            return true;
        }
        last_call_ms = now_ms;

        ElmResponse resp = elm_.command(std::string(spec.request), kElmResponseTimeoutMs);
        if (!resp.ok) {
            fail_count_.fetch_add(1);
            if (resp.error == "NO DATA") return true;                   // PID non supportato
            if (resp.error.find("timeout") != std::string::npos) return false;
            return true;
        }
        uint8_t  raw[64];
        size_t   n = parseHexPayload(resp.raw, raw, sizeof(raw));
        if (n == 0) { fail_count_.fetch_add(1); return true; }
        const uint8_t* data = nullptr;
        if (!extractPidData(raw, n, spec, data)) { fail_count_.fetch_add(1); return true; }
        float val = 0.0f;
        if (spec.decode && spec.decode(data, spec.data_bytes, val)) {
            data::store().set(spec.metric, val);
            success_count_.fetch_add(1);
        } else {
            fail_count_.fetch_add(1);
        }
        return true;
    };

    // Tempo dell'ultima interrogazione per ogni PID (persistente tra chiamate).
    static int64_t last_std[kStandardPidsCount]    = {};
    static int64_t last_cust[kAbarthCustomPidsCount] = {};

    for (size_t i = 0; i < kStandardPidsCount; ++i) {
        if (!try_pid(kStandardPids[i], last_std[i])) return false;
    }
    for (size_t i = 0; i < kAbarthCustomPidsCount; ++i) {
        if (!try_pid(kAbarthCustomPids[i], last_cust[i])) return false;
    }
    return ble_.isConnected();
}

}  // namespace abarth::obd
