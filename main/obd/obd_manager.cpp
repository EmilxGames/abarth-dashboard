#include "obd/obd_manager.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>

#include <esp_log.h>
#include <esp_timer.h>
#include "config/app_config.h"
#include "config/settings.h"
#include "data/data_store.h"
#include "obd/pids.h"
#include "wifi_manager.h"

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

// --- Coda comandi (revisione C5) -------------------------------------------

struct ObdManager::DtcRequest {
    SemaphoreHandle_t      done = xSemaphoreCreateBinary();
    bool                   ok = false;
    std::vector<DtcItem>   codes;
    std::string            msg;
    ~DtcRequest() { if (done) vSemaphoreDelete(done); }
};

struct ObdManager::Command {
    enum class Type : uint8_t { ConnectBle, ConnectWifi, Forget, Stop, ReadDtc, ClearDtc };
    Type                        type;
    std::string                 arg;
    uint16_t                    port = 0;
    std::shared_ptr<DtcRequest> req;
};

ObdManager::ObdManager() : active_stream_(&ble_), elm_(active_stream_) {}

bool ObdManager::ensureQueue() {
    if (!err_mtx_) err_mtx_ = xSemaphoreCreateMutex();
    if (!cmd_q_) cmd_q_ = xQueueCreate(8, sizeof(Command*));
    return cmd_q_ != nullptr && err_mtx_ != nullptr;
}

bool ObdManager::start() {
    if (task_) return true;
    if (!ensureQueue()) return false;
    BaseType_t ok = xTaskCreatePinnedToCore(
        &ObdManager::taskTrampoline, "obd", 8192, this, 4, &task_, 1);
    return ok == pdPASS;
}

void ObdManager::post(Command* cmd) {
    if (!task_) start();  // es. passaggio da CAN diretto a OBD a runtime
    if (!cmd_q_ || xQueueSend(cmd_q_, &cmd, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "coda comandi OBD piena: comando scartato");
        if (cmd->req) {
            cmd->req->msg = "Servizio OBD occupato, riprova.";
            xSemaphoreGive(cmd->req->done);
        }
        delete cmd;
    }
}

void ObdManager::processCommands(TickType_t wait) {
    Command* cmd = nullptr;
    while (cmd_q_ && xQueueReceive(cmd_q_, &cmd, wait) == pdTRUE) {
        if (cmd) {
            execute(*cmd);
            delete cmd;
        }
        wait = 0;
    }
    if (!cmd_q_ && wait) vTaskDelay(wait);
}

void ObdManager::execute(Command& cmd) {
    switch (cmd.type) {
        case Command::Type::ConnectBle:  connectBleImpl(cmd.arg); break;
        case Command::Type::ConnectWifi: connectWifiImpl(cmd.arg, cmd.port); break;
        case Command::Type::Forget:      forgetImpl(); break;
        case Command::Type::Stop:
            if (active_stream_) active_stream_->disconnect();
            manual_connection_req_ = false;
            setState(ObdState::Disconnected);
            break;
        case Command::Type::ReadDtc:
            cmd.req->ok = readDtcImpl(cmd.req->codes, cmd.req->msg);
            xSemaphoreGive(cmd.req->done);
            break;
        case Command::Type::ClearDtc:
            cmd.req->ok = clearDtcImpl(cmd.req->msg);
            xSemaphoreGive(cmd.req->done);
            break;
    }
}

std::string ObdManager::lastError() const {
    if (!err_mtx_) return {};
    xSemaphoreTake(err_mtx_, portMAX_DELAY);
    std::string copy = last_error_;
    xSemaphoreGive(err_mtx_);
    return copy;
}

void ObdManager::setLastError(const std::string& e) {
    if (!err_mtx_) return;
    xSemaphoreTake(err_mtx_, portMAX_DELAY);
    last_error_ = e;
    xSemaphoreGive(err_mtx_);
}

void ObdManager::connectBLE(const std::string& mac) {
    post(new Command{Command::Type::ConnectBle, mac, 0, nullptr});
}

void ObdManager::connectWiFi(const std::string& ip, uint16_t port) {
    post(new Command{Command::Type::ConnectWifi, ip, port, nullptr});
}

void ObdManager::forgetDevice() {
    post(new Command{Command::Type::Forget, "", 0, nullptr});
}

void ObdManager::stopService() {
    if (!task_) return;  // gia' fermo
    post(new Command{Command::Type::Stop, "", 0, nullptr});
}

bool ObdManager::readDtcCodes(std::vector<DtcItem>& out_codes, std::string& out_status_msg) {
    out_codes.clear();
    auto req = std::make_shared<DtcRequest>();
    if (!req->done) { out_status_msg = "Memoria insufficiente"; return false; }
    post(new Command{Command::Type::ReadDtc, "", 0, req});
    if (xSemaphoreTake(req->done, pdMS_TO_TICKS(15000)) != pdTRUE) {
        out_status_msg = "Timeout: il servizio OBD non ha risposto.";
        return false;
    }
    out_codes = req->codes;
    out_status_msg = req->msg;
    return req->ok;
}

bool ObdManager::clearDtcCodes(std::string& out_status_msg) {
    auto req = std::make_shared<DtcRequest>();
    if (!req->done) { out_status_msg = "Memoria insufficiente"; return false; }
    post(new Command{Command::Type::ClearDtc, "", 0, req});
    if (xSemaphoreTake(req->done, pdMS_TO_TICKS(15000)) != pdTRUE) {
        out_status_msg = "Timeout: il servizio OBD non ha risposto.";
        return false;
    }
    out_status_msg = req->msg;
    return req->ok;
}

// --- Implementazioni eseguite SOLO nel task OBD ----------------------------

void ObdManager::connectBleImpl(const std::string& mac) {
    if (active_stream_) active_stream_->disconnect();
    active_stream_ = &ble_;
    if (!mac.empty()) {
        settings::setSavedDongleMac(mac);
    }
    
    using namespace abarth::config;
    BleStreamConfig cfg{};
    cfg.target_mac   = mac.empty() ? settings::current().saved_dongle_mac : mac;
    cfg.device_name  = cfg.target_mac.empty() ? kDefaultBleDeviceName : "";
    cfg.service_uuid = kBleServiceUUID;
    cfg.notify_uuid  = kBleNotifyCharUUID;
    cfg.write_uuid   = kBleWriteCharUUID;
    cfg.scan_time_s  = 10;
    ble_.begin(cfg);

    elm_ = Elm327(active_stream_); // Re-bind
    manual_connection_req_ = true;
    setState(ObdState::Disconnected);
}

void ObdManager::connectWifiImpl(const std::string& ip, uint16_t port) {
    if (active_stream_) active_stream_->disconnect();
    active_stream_ = &wifi_;
    elm_ = Elm327(active_stream_); // Re-bind
    
    WifiStreamConfig cfg;
    cfg.ip_address = ip;
    cfg.port = port;
    wifi_.begin(cfg);

    manual_connection_req_ = true;
    setState(ObdState::Disconnected);
}

void ObdManager::forgetImpl() {
    if (active_stream_) active_stream_->disconnect();
    settings::setSavedDongleMac("");
    settings::setSavedWifi("", "");
    manual_connection_req_ = false;
    setState(ObdState::Disconnected);
}

void ObdManager::setState(ObdState s) {
    const ObdState prev = state_.load();
    if (prev == s) return;
    ESP_LOGI(TAG, "stato: %s -> %s", stateName(prev), stateName(s));
    
    printf("[I][obd] [STATE] %s -> %s (uptime=%lldms)\n",
           stateName(prev), stateName(s),
           static_cast<long long>(esp_timer_get_time() / 1000));
    fflush(stdout);
    
    state_.store(s);
    if (s == ObdState::Polling && prev != ObdState::Polling) {
        data::store().clearSupportFlags();
    }
}

void ObdManager::taskTrampoline(void* arg) {
    static_cast<ObdManager*>(arg)->run();
}

void ObdManager::run() {
    using namespace abarth::config;
    ensureQueue();

    BleStreamConfig cfg{};
    const settings::Settings init_s = settings::current();
    cfg.target_mac   = init_s.saved_dongle_mac;
    cfg.device_name  = init_s.saved_dongle_mac.empty() ? kDefaultBleDeviceName : "";
    cfg.service_uuid = kBleServiceUUID;
    cfg.notify_uuid  = kBleNotifyCharUUID;
    cfg.write_uuid   = kBleWriteCharUUID;
    cfg.scan_time_s  = 5;
    ble_.begin(cfg);

    WifiStreamConfig wcfg{};
    wifi_.begin(wcfg);

    while (true) {
        processCommands(0);
        const settings::Settings s = settings::current();

        switch (state_.load()) {
            case ObdState::Disconnected:
            case ObdState::Error: {
                if (s.connection_mode == settings::ConnectionMode::DirectCan) {
                    // Modalita' attiva e' CAN-Bus Diretto (TWAI): il task OBD resta in standby
                    processCommands(pdMS_TO_TICKS(1000));
                    continue;
                }

                if (!s.auto_connect && !manual_connection_req_) {
                    // Idle mode (riconnessione automatica disattivata da impostazioni)
                    processCommands(pdMS_TO_TICKS(1000));
                    continue;
                }
                
                // Il trasporto dipende SOLO dalla modalita' scelta (revisione A5)
                if (s.connection_mode == settings::ConnectionMode::WifiObd) {
                    active_stream_ = &wifi_;
                    auto& wm = abarth::wifi::WifiManager::instance();
                    if (!wm.isConnected() && !s.saved_wifi_ssid.empty()) {
                        // All'avvio la rete del dongle non e' connessa: riconnetti
                        ESP_LOGI(TAG, "riconnessione alla rete WiFi salvata %s", s.saved_wifi_ssid.c_str());
                        wm.connect(s.saved_wifi_ssid, s.saved_wifi_pass, nullptr);
                        for (int i = 0; i < 100 && !wm.isConnected(); ++i) {
                            vTaskDelay(pdMS_TO_TICKS(100));
                        }
                    }
                } else {
                    active_stream_ = &ble_;
                    BleStreamConfig ble_cfg{};
                    ble_cfg.target_mac   = s.saved_dongle_mac;
                    ble_cfg.device_name  = s.saved_dongle_mac.empty() ? kDefaultBleDeviceName : "";
                    ble_cfg.service_uuid = kBleServiceUUID;
                    ble_cfg.notify_uuid  = kBleNotifyCharUUID;
                    ble_cfg.write_uuid   = kBleWriteCharUUID;
                    ble_cfg.scan_time_s  = 5;
                    ble_.begin(ble_cfg);
                }
                elm_ = Elm327(active_stream_);

                setState(ObdState::Scanning);
                break;
            }
            case ObdState::Scanning: {
                if (active_stream_ && active_stream_->connect()) {
                    setState(ObdState::Initializing);
                } else {
                    setLastError("dongle non trovato / non connesso");
                    setState(ObdState::Disconnected);
                    processCommands(pdMS_TO_TICKS(3000));
                }
                break;
            }
            case ObdState::Initializing: {
                if (elm_.init(3500)) {
                    setState(ObdState::Polling);
                } else {
                    setLastError("init ELM327 fallita");
                    if (active_stream_) active_stream_->disconnect();
                    setState(ObdState::Disconnected);
                    processCommands(pdMS_TO_TICKS(2000));
                }
                break;
            }
            case ObdState::Polling: {
                if (!pollOnce()) {
                    setLastError("connessione persa");
                    if (active_stream_) active_stream_->disconnect();
                    setState(ObdState::Disconnected);
                    processCommands(pdMS_TO_TICKS(2000));
                }
                processCommands(pdMS_TO_TICKS(kPollIntervalMs));
                break;
            }
            case ObdState::Connecting: {
                setState(ObdState::Initializing);
                break;
            }
        }
    }
}

bool ObdManager::pollOnce() {
    using namespace abarth::config;

    if (!active_stream_ || !active_stream_->isConnected()) {
        return false;
    }

    // === POLLING BATCH MULTI-PID ===
    // Invia una singola query che chiede TUTTI i PID caldi (Mode 01) in un colpo.
    // L'ELM327 interroga l'ECU una volta e ritorna una riga per ogni PID:
    //   410C1AF4 / 410D00 / 410445 / 411140 / 410B92
    // Questo riduce da N transazioni CAN a 1 sola, ~raddoppiando la frequenza
    // di aggiornamento dei valori che guardi in tempo reale.
    //
    // Fallback: se il dongle non supporta il batch (risponde NO DATA / errore),
    // si passa al polling singolo dei PID caldi nel loop piu' sotto.
    auto poll_batch = [&]() -> bool {
        // Costruisce la query combinando i PID Mode 01 priority supportati.
        // Esempio: "010C0D04110B". Salta i PID gia' marcati non supportati.
        std::string query = "01";
        size_t included = 0;
        for (size_t i = 0; i < kStandardPidsCount; ++i) {
            const auto& sp = kStandardPids[i];
            if (!sp.is_priority) continue;
            if (sp.response_mode != 0x41) continue;  // solo Mode 01
            abarth::data::Sample s = data::store().get(sp.metric);
            if (!s.supported) continue;  // salta i non supportati
            // pid_id a 8 bit per Mode 01
            char hex[4];
            std::snprintf(hex, sizeof(hex), "%02X", sp.pid_id & 0xFF);
            query += hex;
            ++included;
        }
        if (included == 0) return true;  // nulla da fare

        ElmResponse resp = elm_.command(query, kElmResponseTimeoutFastMs);
        if (!resp.ok) {
            // Batch fallito (timeout/errore): il loop singolo sotto provera'
            // comunque i PID caldi uno per uno come fallback.
            return resp.no_data ? true : true;  // non disconnettere per questo
        }

        // Splitta la risposta per righe (separatore \r o \n) e dispatcha.
        // Ogni riga e' una risposta PID tipo "410C1AF4".
        std::string& raw = resp.raw;
        size_t pos = 0;
        size_t line_start = 0;
        uint8_t buf[64];
        for (pos = 0; pos <= raw.size(); ++pos) {
            if (pos == raw.size() || raw[pos] == '\r' || raw[pos] == '\n') {
                size_t line_len = pos - line_start;
                if (line_len > 0) {
                    std::string_view line(raw.data() + line_start, line_len);
                    size_t n = parseHexPayload(line, buf, sizeof(buf), 0x41);
                    if (n >= 2) {
                        const PidSpec* spec = findPidSpecByResponse(buf, n, kStandardPids, kStandardPidsCount);
                        if (spec) {
                            const uint8_t* d = nullptr;
                            if (extractPidData(buf, n, *spec, d)) {
                                float val = 0.0f;
                                if (spec->decode && spec->decode(d, spec->data_bytes, val)) {
                                    data::store().set(spec->metric, val);
                                    success_count_.fetch_add(1);
                                }
                            }
                        }
                    }
                }
                line_start = pos + 1;
            }
        }
        return true;
    };

    // Esegue il batch dei PID caldi ad ogni ciclo.
    poll_batch();

    auto try_pid = [&](const PidSpec& spec, int64_t& last_call_ms,
                       uint8_t& fail_count, uint8_t& link_fail_count) -> bool {
        const int64_t now_ms = esp_timer_get_time() / 1000;

        abarth::data::Sample s = data::store().get(spec.metric);
        // Se gia' marcato come non supportato dalla centralina, lo riprovo
        // solo periodicamente (per recuperarlo se era un NO DATA transitorio).
        if (!s.supported) {
            if (kNotSupportedRetryMs > 0 &&
                (now_ms - s.updated_ms) < static_cast<int64_t>(kNotSupportedRetryMs)) {
                return true;  // ancora in cooldown, salta
            }
            // scaduto il cooldown: lo riprovo
        }

        if (spec.period_ms > 0 && (now_ms - last_call_ms) < static_cast<int64_t>(spec.period_ms)) {
            return true;
        }
        last_call_ms = now_ms;

        // Timeout differenziato: PID caldi (priority) usano un timeout piu' breve
        // per avere risposte piu' reattive; PID lenti usano il timeout generico.
        const uint32_t timeout = spec.is_priority ? kElmResponseTimeoutFastMs
                                                    : kElmResponseTimeoutMs;
        ElmResponse resp = elm_.command(std::string(spec.request), timeout);
        if (!resp.ok) {
            fail_count_.fetch_add(1);
            fail_count++;

            if (resp.no_data) {
                // L'ECU ha risposto "NO DATA": il PID non e' realmente supportato.
                // Marca come non supportato solo dopo parecchi tentativi per
                // evitare falsi positivi (i cloni ELM327 possono rispondere
                // NODATA transitoriamente quando il bus e' occupato).
                if (fail_count >= kMaxNoDataBeforeBackoff) {
                    ESP_LOGW(TAG, "PID %.*s NON SUPPORTATO dall'ECU (NO DATA x%d)",
                             (int)spec.name.size(), spec.name.data(), fail_count);
                    data::store().markNotSupported(spec.metric);
                    fail_count = 0;  // verra' ritentato dopo kNotSupportedRetryMs
                }
                return true;
            }

            // Errore di trasporto (timeout, "?", CANERROR, ecc.):
            // NON marcare il PID come non supportato, ma conta come "link fail".
            link_fail_count++;
            ESP_LOGW(TAG, "PID %.*s errore link '%s' (link_fails=%d)",
                     (int)spec.name.size(), spec.name.data(),
                     resp.error.c_str(), link_fail_count);
            if (link_fail_count >= kMaxFailBeforeDisconnect) {
                // Il dongle BLE sembra effettivamente morto: disconnetti.
                ESP_LOGE(TAG, "Troppi errori link consecutivi: disconnetto dongle");
                return false;
            }
            // Pausa extra dopo un errore di link per dare tempo al dongle di riprendersi
            vTaskDelay(pdMS_TO_TICKS(150));
            return true;
        }

        // OK: resetta i contatori di errore di link
        link_fail_count = 0;

        uint8_t raw[64];
        size_t n = parseHexPayload(resp.raw, raw, sizeof(raw), spec.response_mode);
        if (n == 0) {
            fail_count_.fetch_add(1);
            fail_count++;
            if (fail_count >= kMaxNoDataBeforeBackoff) {
                data::store().markNotSupported(spec.metric);
                fail_count = 0;
            }
            return true;
        }

        const uint8_t* data = nullptr;
        if (!extractPidData(raw, n, spec, data)) {
            fail_count_.fetch_add(1);
            fail_count++;
            if (fail_count >= kMaxNoDataBeforeBackoff) {
                data::store().markNotSupported(spec.metric);
                fail_count = 0;
            }
            return true;
        }

        float val = 0.0f;
        if (spec.decode && spec.decode(data, spec.data_bytes, val)) {
            data::store().set(spec.metric, val);
            success_count_.fetch_add(1);
            fail_count = 0; // Successo: resetta il contatore fallimenti
        } else {
            fail_count_.fetch_add(1);
            fail_count++;
            if (fail_count >= kMaxNoDataBeforeBackoff) {
                data::store().markNotSupported(spec.metric);
                fail_count = 0;
            }
        }

        // Pausa tra un PID e il successivo: evita overflow del dongle BLE.
        // I PID caldi usano una pausa piu' breve, i lenti una piu' lunga.
        const uint32_t cooldown = spec.is_priority ? kPidCooldownFastMs : kPidCooldownMs;
        vTaskDelay(pdMS_TO_TICKS(cooldown));
        return true;
    };

    static int64_t last_std[kStandardPidsCount]       = {};
    static uint8_t fails_std[kStandardPidsCount]      = {};
    static uint8_t link_fails_std[kStandardPidsCount] = {};
    static int64_t last_cust[kAbarthCustomPidsCount]  = {};
    static uint8_t fails_cust[kAbarthCustomPidsCount] = {};
    static uint8_t link_fails_cust[kAbarthCustomPidsCount] = {};

    // --- Logging diagnostico periodico (ogni ~5s) ---
    // Mostra chiaramente quali PID rispondono e quali no, per debug in macchina.
    static int64_t last_diag_log_ms = 0;
    const int64_t now_for_diag = esp_timer_get_time() / 1000;
    if (now_for_diag - last_diag_log_ms > 5000) {
        last_diag_log_ms = now_for_diag;
        ESP_LOGD(TAG, "=== DIAGNOSTICA PID ===");
        for (size_t i = 0; i < kStandardPidsCount; ++i) {
            const auto& spec = kStandardPids[i];
            abarth::data::Sample s = data::store().get(spec.metric);
            const char* status = s.supported ? "OK " : "NO-DATA";
            if (s.valid()) {
                ESP_LOGD(TAG, "  [%s] %-22s = %.1f %.*s  (req=%.*s)",
                         status,
                         spec.name.data(),
                         s.value,
                         (int)spec.unit.size(), spec.unit.data(),
                         (int)spec.request.size(), spec.request.data());
            } else {
                ESP_LOGD(TAG, "  [%s] %-22s = N/A        (req=%.*s)",
                         status,
                         spec.name.data(),
                         (int)spec.request.size(), spec.request.data());
            }
        }
        for (size_t i = 0; i < kAbarthCustomPidsCount; ++i) {
            const auto& spec = kAbarthCustomPids[i];
            abarth::data::Sample s = data::store().get(spec.metric);
            const char* status = s.supported ? "OK " : "NO-DATA";
            if (s.valid()) {
                ESP_LOGD(TAG, "  [%s] %-22s = %.1f %.*s  (req=%.*s)",
                         status,
                         spec.name.data(),
                         s.value,
                         (int)spec.unit.size(), spec.unit.data(),
                         (int)spec.request.size(), spec.request.data());
            } else {
                ESP_LOGD(TAG, "  [%s] %-22s = N/A        (req=%.*s)",
                         status,
                         spec.name.data(),
                         (int)spec.request.size(), spec.request.data());
            }
        }
        ESP_LOGD(TAG, "=== tot OK=%lu ERR=%lu ===",
                 (unsigned long)success_count_.load(), (unsigned long)fail_count_.load());
    }

    // === POLLING PRIORIZZATO ===
    // 1) PID "caldi" (priority): gia' gestiti dal batch multi-PID qui sopra.
    //    Fallback: se il batch ha fallito (il valore non e' fresco), proviamo
    //    i PID caldi singolarmente. Questo capita se il dongle non supporta
    //    il batch o se c'e' stato un timeout.
    {
        const int64_t now_ms = esp_timer_get_time() / 1000;
        // Priority standard (RPM, velocità, MAP/Boost, carico, throttle)
        for (size_t i = 0; i < kStandardPidsCount; ++i) {
            if (!kStandardPids[i].is_priority) continue;
            abarth::data::Sample s = data::store().get(kStandardPids[i].metric);
            // Se il valore e' fresco (< 300ms) il batch ha funzionato: salta.
            if (s.supported && s.valid() && (now_ms - s.updated_ms) < 300) continue;
            if (!try_pid(kStandardPids[i], last_std[i], fails_std[i], link_fails_std[i]))
                return false;
        }
        // Priority custom Abarth (es. Boost reale Mode 22)
        for (size_t i = 0; i < kAbarthCustomPidsCount; ++i) {
            if (!kAbarthCustomPids[i].is_priority) continue;
            abarth::data::Sample s = data::store().get(kAbarthCustomPids[i].metric);
            if (s.supported && s.valid() && (now_ms - s.updated_ms) < 300) continue;
            if (!try_pid(kAbarthCustomPids[i], last_cust[i], fails_cust[i], link_fails_cust[i]))
                return false;
        }
    }

    // 2) PID "lenti" (non priority): interrogati a rotazione, solo
    //    kSlowPidsPerCycle per ciclo. Questo evita che i PID lenti (temperature,
    //    livello carburante, ecc.) blocchino l'aggiornamento dei valori caldi.
    //    L'indice statico fa avanzare la rotazione tra un ciclo e il successivo.
    static size_t slow_std_cursor  = 0;
    static size_t slow_cust_cursor = 0;
    uint8_t slow_done = 0;

    // Lenti standard (saltando i priority)
    for (size_t step = 0; step < kStandardPidsCount && slow_done < kSlowPidsPerCycle; ++step) {
        size_t i = (slow_std_cursor + step) % kStandardPidsCount;
        if (kStandardPids[i].is_priority) continue;
        slow_std_cursor = (i + 1) % kStandardPidsCount;
        if (!try_pid(kStandardPids[i], last_std[i], fails_std[i], link_fails_std[i]))
            return false;
        ++slow_done;
    }
    // Lenti custom (Abarth Mode 22 - saltando i priority)
    if (slow_done < kSlowPidsPerCycle) {
        for (size_t step = 0; step < kAbarthCustomPidsCount && slow_done < kSlowPidsPerCycle; ++step) {
            size_t i = (slow_cust_cursor + step) % kAbarthCustomPidsCount;
            if (kAbarthCustomPids[i].is_priority) continue;
            slow_cust_cursor = (i + 1) % kAbarthCustomPidsCount;
            if (!try_pid(kAbarthCustomPids[i], last_cust[i], fails_cust[i], link_fails_cust[i]))
                return false;
            ++slow_done;
        }
    }

    // --- Consumo L/100km calcolato ---
    // Formula: consumo = (portata_L_h / velocita_km_h) * 100
    // Valido solo se abbiamo sia portata che velocita' > 0.
    {
        auto rate = data::store().get(data::Metric::FuelRateLh);
        auto spd  = data::store().get(data::Metric::Speed);
        if (rate.valid() && spd.valid() && spd.value > 1.0f) {
            float l100 = (rate.value / spd.value) * 100.0f;
            if (l100 >= 0.0f && l100 < 100.0f) {  // sanity check
                data::store().set(data::Metric::FuelConsumptionL100, l100);
            }
        } else if (rate.valid() && (!spd.valid() || spd.value <= 1.0f)) {
            // A veicolo fermo il consumo e' "infinito" -> mostriamo 0 o lo nascondiamo
            data::store().set(data::Metric::FuelConsumptionL100, 0.0f);
        }
    }

    return active_stream_ && active_stream_->isConnected();
}

namespace {
std::string lookupDtcDescription(const std::string& code) {
    // Specifici Turbo / Sovralimentazione Abarth 595 T-Jet / Multiair
    if (code == "P0234") return "Overboost: Pressione sovralimentazione turbo troppo elevata";
    if (code == "P0235") return "Sensore pressione turbo / MAP A: Guasto circuito";
    if (code == "P0236") return "Sensore pressione turbo A: Segnale fuori plausibilità";
    if (code == "P0237") return "Sensore pressione turbo A: Segnale troppo basso (Corto a massa)";
    if (code == "P0238") return "Sensore pressione turbo A: Segnale troppo alto (Circuito aperto / Picco)";
    if (code == "P0243") return "Valvola Pierburg / Attuatore wastegate A: Malfunzionamento";
    if (code == "P0245") return "Valvola Pierburg wastegate: Segnale basso / Cortocircuito";
    if (code == "P0246") return "Valvola Pierburg wastegate: Segnale alto / Sovraccarico";
    if (code == "P0299") return "Pressione turbo insufficiente (Underboost / Perdita manicotto o intercooler)";

    // Misfire & Accensione
    if (code == "P0300") return "Mancata accensione cilindri multipla o casuale (Misfire)";
    if (code == "P0301") return "Mancata accensione Cilindro 1 (Bobina / Candela / Iniettore)";
    if (code == "P0302") return "Mancata accensione Cilindro 2 (Bobina / Candela / Iniettore)";
    if (code == "P0303") return "Mancata accensione Cilindro 3 (Bobina / Candela / Iniettore)";
    if (code == "P0304") return "Mancata accensione Cilindro 4 (Bobina / Candela / Iniettore)";
    if (code == "P0325") return "Sensore di detonazione / battito 1 (Knock Sensor): Guasto";
    if (code == "P0335") return "Sensore giri albero motore (CKP): Circuito guasto / Assenza segnale";
    if (code == "P0340") return "Sensore fase albero a camme (CMP): Guasto circuito";

    // Carburazione, Miscela & Lambda
    if (code == "P0171") return "Carburazione magra: Sistema troppo magro Banco 1 (Aspirazione aria/pompa)";
    if (code == "P0172") return "Carburazione grassa: Sistema troppo grasso Banco 1";
    if (code == "P0130") return "Sonda Lambda a monte (B1 S1): Malfunzionamento circuito";
    if (code == "P0133") return "Sonda Lambda a monte (B1 S1): Risposta lenta";
    if (code == "P0135") return "Riscaldatore Sonda Lambda a monte (B1 S1): Circuito interrotto";
    if (code == "P0136") return "Sonda Lambda a valle (B1 S2): Malfunzionamento";
    if (code == "P0141") return "Riscaldatore Sonda Lambda a valle (B1 S2): Guasto";
    if (code == "P0420") return "Efficienza catalizzatore inferiore alla soglia (Skat / Downpipe 200 celle)";

    // Sensori Pressione, Aria, Temperatura
    if (code == "P0105") return "Sensore pressione assoluta collettore (MAP): Circuito difettoso";
    if (code == "P0106") return "Sensore pressione collettore (MAP): Segnale fuori range / perdita";
    if (code == "P0110") return "Sensore temperatura aria aspirata (IAT): Circuito guasto";
    if (code == "P0115") return "Sensore temperatura refrigerante motore (ECT): Guasto";
    if (code == "P0116") return "Sensore temperatura refrigerante motore: Segnale incoerente / Termostato";
    if (code == "P0120") return "Potenziometro posizione farfalla (TPS A): Circuito difettoso";
    if (code == "P0220") return "Potenziometro posizione farfalla (TPS B): Circuito difettoso";
    if (code == "P0520") return "Sensore / Manocontatto pressione olio motore: Guasto circuito";

    // Iniettori & Alimentazione Carburante
    if (code == "P0201") return "Circuito iniettore Cilindro 1 guasto";
    if (code == "P0202") return "Circuito iniettore Cilindro 2 guasto";
    if (code == "P0203") return "Circuito iniettore Cilindro 3 guasto";
    if (code == "P0204") return "Circuito iniettore Cilindro 4 guasto";
    if (code == "P0443") return "Valvola spurgo vapori benzina canister (EVAP): Guasto";

    // Specifici FIAT / Abarth (Modulo MultiAir & Ruota Fonica)
    if (code == "P1001") return "Modulo UniAir/MultiAir: Malfunzionamento attuatore Cilindro 1";
    if (code == "P1002") return "Modulo UniAir/MultiAir: Malfunzionamento attuatore Cilindro 2";
    if (code == "P1003") return "Modulo UniAir/MultiAir: Malfunzionamento attuatore Cilindro 3";
    if (code == "P1004") return "Modulo UniAir/MultiAir: Malfunzionamento attuatore Cilindro 4";
    if (code == "P1066") return "Modulo UniAir/MultiAir: Segnale elettrovalvola Cilindro 1";
    if (code == "P1067") return "Modulo UniAir/MultiAir: Segnale elettrovalvola Cilindro 2";
    if (code == "P1068") return "Modulo UniAir/MultiAir: Segnale elettrovalvola Cilindro 3";
    if (code == "P1069") return "Modulo UniAir/MultiAir: Segnale elettrovalvola Cilindro 4";
    if (code == "P1300") return "Apprendimento ruota fonica: Calibrazione richiesta (Autoapprendimento)";
    if (code == "P1220") return "Sensore posizione pedale acceleratore: Incoerenza traccia";

    // Centralina, Alimentazione & Rete CAN Bus
    if (code == "P0500") return "Sensore velocità veicolo (VSS): Nessun segnale";
    if (code == "P0562") return "Tensione impianto elettrico troppo bassa (< 10V)";
    if (code == "P0563") return "Tensione impianto elettrico troppo alta (> 16V / Alternatore)";
    if (code == "P0601") return "Errore checksum memoria ROM centralina motore";
    if (code == "P0606") return "Microprocessore interno centralina motore (ECU / PCM)";
    if (code == "U0001") return "Bus dati CAN alta velocità: Errore di comunicazione";
    if (code == "U0100") return "Perdita di comunicazione con Centralina Motore (ECM/PCM)";
    if (code == "U0422") return "Dati non validi ricevuti dal Body Computer (NBC)";
    if (code == "U0426") return "Comunicazione Immobilizer / Chiave elettronica non riconosciuta";

    // Categorie generiche standard SAE
    if (code.rfind("P0", 0) == 0) return "Errore Propulsore / Motore standard OBD-II";
    if (code.rfind("P1", 0) == 0) return "Errore Motore specifico costruttore FIAT / Abarth";
    if (code.rfind("C", 0) == 0)  return "Errore Telaio / Freni / ABS / ESC / Sensore imbardata";
    if (code.rfind("B", 0) == 0)  return "Errore Carrozzeria / Body Computer / Clima / Airbag";
    if (code.rfind("U", 0) == 0)  return "Errore Rete di comunicazione CAN Bus veicolo";
    return "Codice guasto diagnostico centralina";
}

bool parseDtcResponse(const std::string& raw_resp, std::vector<DtcItem>& out_codes) {
    uint8_t bytes[128];
    size_t n = parseHexPayload(raw_resp, bytes, sizeof(bytes));
    if (n < 1) return false;
    size_t start_idx = 0;
    if (bytes[0] == 0x43 || bytes[0] == 0x47) {
        start_idx = 1;
        if (n > 1 && bytes[1] <= 16) {
            start_idx = 2;
        }
    }

    for (size_t i = start_idx; i + 1 < n; i += 2) {
        uint8_t b1 = bytes[i];
        uint8_t b2 = bytes[i+1];
        if (b1 == 0x00 && b2 == 0x00) continue;

        char type_char = 'P';
        uint8_t type_bits = (b1 & 0xC0) >> 6;
        if (type_bits == 1) type_char = 'C';
        else if (type_bits == 2) type_char = 'B';
        else if (type_bits == 3) type_char = 'U';

        uint8_t d1 = (b1 & 0x30) >> 4;
        uint8_t d2 = b1 & 0x0F;
        uint8_t d3 = (b2 & 0xF0) >> 4;
        uint8_t d4 = b2 & 0x0F;

        char code_buf[8];
        std::snprintf(code_buf, sizeof(code_buf), "%c%X%X%X%X", type_char, d1, d2, d3, d4);
        std::string code_str(code_buf);

        bool exists = false;
        for (const auto& existing : out_codes) {
            if (existing.code == code_str) { exists = true; break; }
        }
        if (!exists) {
            out_codes.push_back({code_str, lookupDtcDescription(code_str)});
        }
    }
    return true;
}
}  // namespace

bool ObdManager::readDtcImpl(std::vector<DtcItem>& out_codes, std::string& out_status_msg) {
    out_codes.clear();
    if (abarth::settings::current().demo_mode) {
        out_status_msg = "Simulazione: Nessun errore presente in memoria centralina.";
        return true;
    }

    if (!active_stream_ || !active_stream_->isConnected()) {
        out_status_msg = "Errore: Dongle OBD non connesso.";
        return false;
    }

    ElmResponse r3 = elm_.command("03", config::kElmResponseTimeoutMs);
    if (r3.ok && !r3.no_data) {
        parseDtcResponse(r3.raw, out_codes);
    }

    ElmResponse r7 = elm_.command("07", config::kElmResponseTimeoutMs);
    if (r7.ok && !r7.no_data) {
        parseDtcResponse(r7.raw, out_codes);
    }

    if (out_codes.empty()) {
        out_status_msg = "Nessun codice errore (DTC) presente in memoria centralina!";
    } else {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "Rilevati %u codici errore DTC in memoria centralina:", (unsigned)out_codes.size());
        out_status_msg = buf;
    }
    return true;
}

bool ObdManager::dtcClearAllowed(std::string& out_reason) const {
    if (abarth::settings::current().demo_mode) return true;
    if (state() != ObdState::Polling) {
        out_reason = "Cancellazione non disponibile: la centralina non e' collegata via OBD.";
        return false;
    }
    const int64_t now_ms = esp_timer_get_time() / 1000;
    const data::Sample rpm = data::store().get(data::Metric::Rpm);
    const data::Sample spd = data::store().get(data::Metric::Speed);
    if (!rpm.fresh(2000, now_ms) || !spd.fresh(2000, now_ms)) {
        out_reason = "Cancellazione bloccata: giri motore e velocita' non sono aggiornati.\n"
                     "Attendi qualche secondo con il quadro acceso e riprova.";
        return false;
    }
    if (rpm.value >= 1.0f || spd.value >= 0.5f) {
        out_reason = "Cancellazione bloccata per sicurezza: serve il motore SPENTO (0 giri)\n"
                     "e l'auto FERMA, con il quadro acceso.";
        return false;
    }
    return true;
}

bool ObdManager::clearDtcImpl(std::string& out_status_msg) {
    if (abarth::settings::current().demo_mode) {
        out_status_msg = "Simulazione: Codici errore ed indicatore MIL azzerati con successo!";
        return true;
    }

    if (!dtcClearAllowed(out_status_msg)) {
        ESP_LOGW(TAG, "Mode 04 rifiutato: %s", out_status_msg.c_str());
        return false;
    }

    if (!active_stream_ || !active_stream_->isConnected()) {
        out_status_msg = "Errore: Dongle OBD non connesso.";
        return false;
    }

    ElmResponse r = elm_.command("04", 3000);
    if (r.ok) {
        out_status_msg = "Memoria errori azzerata e Spia Motore (MIL) resettata con successo!";
        return true;
    } else {
        out_status_msg = "Impossibile azzerare gli errori. Verificare che il quadro sia ACCESO a motore SPENTO.";
        return false;
    }
}

}  // namespace abarth::obd
