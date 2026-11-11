#include "can/can_manager.h"

#include <cstring>
#include <esp_log.h>
#include <esp_timer.h>
#include "config/settings.h"
#include "data/data_store.h"

namespace abarth::can {

namespace {
constexpr const char* TAG = "can_mgr";

// Elenco PID OBD-over-CAN da interrogare ciclicamente in modalita' attiva (su CAN 1 C-CAN)
constexpr uint8_t kObdCanPids[] = {
    0x0C, // RPM
    0x0D, // Velocita'
    0x0B, // MAP / Pressione Collettore
    0x11, // Farfalla
    0x04, // Carico Motore
    0x05, // Temp. Refrigerante
    0x0F, // Temp. Aria Aspirata
    0x42, // Tensione Batteria Modulo
    0x5C, // Temp. Olio Motore (se supportato)
    0x2F, // Livello Carburante %
    0x24, // Sonda Lambda / AFR (Equivalence Ratio)
};
constexpr size_t kObdCanPidsCount = sizeof(kObdCanPids) / sizeof(kObdCanPids[0]);

static twai_timing_config_t getTimingConfig(uint32_t baud) {
    switch (baud) {
        case 50000:   return TWAI_TIMING_CONFIG_50KBITS();
        case 100000:  return TWAI_TIMING_CONFIG_100KBITS();
        case 125000:  return TWAI_TIMING_CONFIG_125KBITS();
        case 250000:  return TWAI_TIMING_CONFIG_250KBITS();
        case 1000000: return TWAI_TIMING_CONFIG_1MBITS();
        case 500000:
        default:      return TWAI_TIMING_CONFIG_500KBITS();
    }
}

}  // namespace

const char* canStateName(CanState s) {
    switch (s) {
        case CanState::Stopped:    return "ARRESTATO";
        case CanState::Starting:   return "AVVIO...";
        case CanState::Running:    return "ATTIVO (OK)";
        case CanState::BusOff:     return "BUS-OFF";
        case CanState::Recovering: return "RIPRISTINO...";
        case CanState::Error:      return "ERRORE";
        default:                   return "SCONOSCIUTO";
    }
}

CanManager::CanManager() = default;

CanManager::~CanManager() {
    stop();
}

bool CanManager::start() {
    bool ok = true;
    const auto& s = settings::current();

    if (s.can1_enabled) {
        if (!startBus(0)) {
            ok = false;
        }
    } else {
        stopBus(0);
    }

    if (s.can2_enabled) {
        if (!startBus(1)) {
            ok = false;
        }
    } else {
        stopBus(1);
    }

    return ok;
}

void CanManager::stop() {
    stopBus(0);
    stopBus(1);
    ESP_LOGI(TAG, "Tutti i canali CAN arrestati");
}

bool CanManager::startBus(uint8_t bus_idx) {
    if (bus_idx > 1) return false;

    if (tasks_[bus_idx] != nullptr) {
        ESP_LOGW(TAG, "CAN %d task gia' in esecuzione", bus_idx + 1);
        return true;
    }

    stop_requested_[bus_idx] = false;
    setBusState(bus_idx, CanState::Starting);

    const char* task_name = (bus_idx == 0) ? "can1_c_task" : "can2_b_task";
    auto trampoline = (bus_idx == 0) ? taskTrampolineCan1 : taskTrampolineCan2;

    BaseType_t ret = xTaskCreatePinnedToCore(
        trampoline,
        task_name,
        4096,
        this,
        tskIDLE_PRIORITY + 3,
        &tasks_[bus_idx],
        tskNO_AFFINITY
    );

    if (ret != pdPASS || !tasks_[bus_idx]) {
        ESP_LOGE(TAG, "Creazione task CAN %d fallita", bus_idx + 1);
        bus_[bus_idx].last_error = "creazione task fallita";
        setBusState(bus_idx, CanState::Error);
        return false;
    }

    ESP_LOGI(TAG, "Task CAN %d avviato con successo", bus_idx + 1);
    return true;
}

void CanManager::stopBus(uint8_t bus_idx) {
    if (bus_idx > 1) return;

    stop_requested_[bus_idx] = true;
    if (tasks_[bus_idx] != nullptr) {
        // Attendi che il task esca dal loop e si autodistrugga
        for (int i = 0; i < 20 && tasks_[bus_idx] != nullptr; ++i) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        tasks_[bus_idx] = nullptr;
    }

    if (twai_handles_[bus_idx] != nullptr) {
        twai_stop_v2(twai_handles_[bus_idx]);
        twai_driver_uninstall_v2(twai_handles_[bus_idx]);
        twai_handles_[bus_idx] = nullptr;
    }

    setBusState(bus_idx, CanState::Stopped);
    ESP_LOGI(TAG, "CAN %d arrestato", bus_idx + 1);
}

bool CanManager::restart() {
    stop();
    vTaskDelay(pdMS_TO_TICKS(150));
    return start();
}

bool CanManager::restartBus(uint8_t bus_idx) {
    if (bus_idx > 1) return false;
    stopBus(bus_idx);
    vTaskDelay(pdMS_TO_TICKS(100));
    return startBus(bus_idx);
}

void CanManager::setBusState(uint8_t bus_idx, CanState s) {
    if (bus_idx > 1) return;
    const CanState prev = bus_[bus_idx].state.load();
    if (prev == s) return;
    ESP_LOGI(TAG, "Stato CAN %d: %s -> %s", bus_idx + 1, canStateName(prev), canStateName(s));
    bus_[bus_idx].state.store(s);
}

CanState CanManager::state(uint8_t bus_idx) const {
    if (bus_idx > 1) return CanState::Stopped;
    return bus_[bus_idx].state.load();
}

bool CanManager::isRunning(uint8_t bus_idx) const {
    return state(bus_idx) == CanState::Running;
}

uint32_t CanManager::rxFrames(uint8_t bus_idx) const {
    if (bus_idx > 1) return 0;
    return bus_[bus_idx].rx_count.load(std::memory_order_relaxed);
}

uint32_t CanManager::txFrames(uint8_t bus_idx) const {
    if (bus_idx > 1) return 0;
    return bus_[bus_idx].tx_count.load(std::memory_order_relaxed);
}

uint32_t CanManager::busErrors(uint8_t bus_idx) const {
    if (bus_idx > 1) return 0;
    return bus_[bus_idx].err_count.load(std::memory_order_relaxed);
}

float CanManager::rxRateFps(uint8_t bus_idx) const {
    if (bus_idx > 1) return 0.0f;
    return bus_[bus_idx].rx_fps.load(std::memory_order_relaxed);
}

const std::string& CanManager::lastError(uint8_t bus_idx) const {
    static const std::string empty;
    if (bus_idx > 1) return empty;
    return bus_[bus_idx].last_error;
}

bool CanManager::transmit(uint8_t bus_idx, uint32_t id, bool extd, const uint8_t* data, uint8_t len) {
    if (bus_idx > 1) return false;
    if (bus_[bus_idx].state.load() != CanState::Running || twai_handles_[bus_idx] == nullptr) {
        return false;
    }

    twai_message_t tx_msg{};
    tx_msg.identifier = id;
    tx_msg.extd = extd ? 1 : 0;
    tx_msg.rtr = 0;
    tx_msg.data_length_code = (len > 8) ? 8 : len;
    if (data && len > 0) {
        memcpy(tx_msg.data, data, tx_msg.data_length_code);
    }

    esp_err_t err = twai_transmit_v2(twai_handles_[bus_idx], &tx_msg, pdMS_TO_TICKS(15));
    if (err == ESP_OK) {
        bus_[bus_idx].tx_count.fetch_add(1, std::memory_order_relaxed);
        return true;
    }
    bus_[bus_idx].err_count.fetch_add(1, std::memory_order_relaxed);
    return false;
}

void CanManager::sendNextObdQuery() {
    const auto& s = settings::current();
    if (s.can_listen_only) return; // In modalita' solo ascolto non trasmettiamo

    const uint8_t pid = kObdCanPids[current_pid_index_];
    current_pid_index_ = (current_pid_index_ + 1) % kObdCanPidsCount;

    // Standard 11-bit OBD-II Functional Query su C-CAN (bus 0): ID 0x7DF
    // Payload: [02] [01] [PID] [55 55 55 55 55]
    uint8_t q_data[8] = { 0x02, 0x01, pid, 0x55, 0x55, 0x55, 0x55, 0x55 };
    transmit(0, 0x7DF, false, q_data, 8);
}

void CanManager::handleReceivedMessageCan1(const twai_message_t& msg) {
    bus_[0].rx_count.fetch_add(1, std::memory_order_relaxed);
    auto& store = data::store();

    const uint32_t id = msg.identifier;
    const uint8_t* d  = msg.data;
    const uint8_t  dlc = msg.data_length_code;

    /* -------------------------------------------------------------------------
     * 1. Decodifica Standard OBD-over-CAN (ISO 15765-4 su ID 0x7E8 / 0x7E9)
     * ------------------------------------------------------------------------- */
    if (id == 0x7E8 || id == 0x7E9) {
        if (dlc >= 3 && d[1] == 0x41) { // Service 01 Response
            const uint8_t pid = d[2];
            const uint8_t* val = &d[3];
            const size_t vlen = (dlc >= 3) ? (dlc - 3) : 0;

            switch (pid) {
                case 0x0C: // RPM
                    if (vlen >= 2) {
                        float rpm = ((val[0] * 256) + val[1]) / 4.0f;
                        store.set(data::Metric::Rpm, rpm);
                    }
                    break;
                case 0x0D: // Velocita' (km/h)
                    if (vlen >= 1) {
                        store.set(data::Metric::Speed, static_cast<float>(val[0]));
                    }
                    break;
                case 0x0B: // Pressione Assoluta MAP (kPa)
                    if (vlen >= 1) {
                        float kpa = static_cast<float>(val[0]);
                        store.set(data::Metric::BoostKpa, kpa);
                        float rel_bar = (kpa - 101.3f) / 100.0f;
                        if (rel_bar < 0.0f) rel_bar = 0.0f;
                        store.set(data::Metric::BoostRelBar, rel_bar);
                    }
                    break;
                case 0x04: // Carico Motore (%)
                    if (vlen >= 1) {
                        store.set(data::Metric::EngineLoad, val[0] * 100.0f / 255.0f);
                    }
                    break;
                case 0x11: // Posizione Farfalla (%)
                    if (vlen >= 1) {
                        store.set(data::Metric::Throttle, val[0] * 100.0f / 255.0f);
                    }
                    break;
                case 0x05: // Temperatura Refrigerante (°C)
                    if (vlen >= 1) {
                        store.set(data::Metric::CoolantTempC, static_cast<float>(val[0]) - 40.0f);
                    }
                    break;
                case 0x0F: // Temperatura Aria Aspirata (°C)
                    if (vlen >= 1) {
                        store.set(data::Metric::IntakeAirTempC, static_cast<float>(val[0]) - 40.0f);
                    }
                    break;
                case 0x42: // Tensione Modulo Controllo (V)
                    if (vlen >= 2) {
                        float v = ((val[0] * 256) + val[1]) / 1000.0f;
                        store.set(data::Metric::BatteryVolts, v);
                    }
                    break;
                case 0x2F: // Livello Carburante (%)
                    if (vlen >= 1) {
                        store.set(data::Metric::FuelLevelPct, val[0] * 100.0f / 255.0f);
                    }
                    break;
                case 0x5C: // Temperatura Olio Motore (°C)
                    if (vlen >= 1) {
                        store.set(data::Metric::OilTempC, static_cast<float>(val[0]) - 40.0f);
                    }
                    break;
                case 0x24: // AFR / Rapporto Stechiometrico (Equivalence Ratio)
                case 0x34:
                    if (vlen >= 2) {
                        float lambda = ((val[0] * 256) + val[1]) / 32768.0f;
                        store.set(data::Metric::Afr, lambda * 14.7f);
                    }
                    break;
                default:
                    break;
            }
        }
        return;
    }

    /* -------------------------------------------------------------------------
     * 2. Sniffing Passivo Frames Nativi Abarth / FCA Powertrain C-CAN (500 kbps)
     * ------------------------------------------------------------------------- */
    // Frame RPM & Acceleratore (standard 11-bit 0x0A0 o extended 0x0618A001)
    if (id == 0x0A0 || id == 0x0618A001) {
        if (dlc >= 4) {
            float rpm = static_cast<float>((d[2] << 8) | d[3]) / 4.0f;
            if (rpm >= 300.0f && rpm <= 9000.0f) {
                store.set(data::Metric::Rpm, rpm);
            }
        }
        if (dlc >= 5) {
            float th = static_cast<float>(d[4]) * 100.0f / 255.0f;
            store.set(data::Metric::Throttle, th);
        }
    }
    // Frame Velocita' Veicolo (standard 11-bit 0x284 o extended 0x0218A006)
    else if (id == 0x284 || id == 0x0218A006) {
        if (dlc >= 2) {
            float spd = static_cast<float>((d[0] << 8) | d[1]) * 0.01f;
            if (spd >= 0.0f && spd <= 320.0f) {
                store.set(data::Metric::Speed, spd);
            }
        }
    }
    // Frame Temperatura Liquido Raffreddamento (0x288 o 0x0218A008)
    else if (id == 0x288 || id == 0x0218A008) {
        if (dlc >= 1) {
            float temp = static_cast<float>(d[0]) - 40.0f;
            if (temp >= -30.0f && temp <= 150.0f) {
                store.set(data::Metric::CoolantTempC, temp);
            }
        }
    }
    // Frame Turbo Boost / Pressione Collettore (0x584 o 0x484)
    else if (id == 0x584 || id == 0x484) {
        if (dlc >= 3) {
            float kpa = static_cast<float>((d[1] << 8) | d[2]) * 0.1f;
            if (kpa >= 20.0f && kpa <= 400.0f) {
                store.set(data::Metric::BoostKpa, kpa);
                float rel_bar = (kpa - 101.3f) / 100.0f;
                if (rel_bar < 0.0f) rel_bar = 0.0f;
                store.set(data::Metric::BoostRelBar, rel_bar);
            }
        }
    }
}

void CanManager::handleReceivedMessageCan2(const twai_message_t& msg) {
    bus_[1].rx_count.fetch_add(1, std::memory_order_relaxed);
    auto& store = data::store();

    const uint32_t id = msg.identifier;
    const uint8_t* d  = msg.data;
    const uint8_t  dlc = msg.data_length_code;

    /* -------------------------------------------------------------------------
     * Sniffing Passivo Frames Nativi B-CAN Carrozzeria Abarth / Fiat 500 (50 kbps)
     * ------------------------------------------------------------------------- */
    // Frame Odometro & Livello Serbatoio B-CAN (ID tipico 0x420 o 0x5A0)
    if (id == 0x420 && dlc >= 4) {
        uint32_t odo = (d[0] << 16) | (d[1] << 8) | d[2];
        if (odo > 0 && odo < 1000000) {
            // Se disponibile un metric odometro o logico
        }
    }
    // Frame Velocita' Veicolo B-CAN (ID 0x284 come backup o replica body)
    else if (id == 0x284 && dlc >= 2) {
        // Se CAN 1 e' disattivato, usiamo la velocita' ricevuta da B-CAN
        if (!isRunning(0)) {
            float spd = static_cast<float>((d[0] << 8) | d[1]) * 0.01f;
            if (spd >= 0.0f && spd <= 320.0f) {
                store.set(data::Metric::Speed, spd);
            }
        }
    }
    // Frame Tensione Batteria / Stato Body Computer (ID 0x2E0 o 0x384)
    else if (id == 0x384 && dlc >= 3) {
        // Tensione batteria da Body Computer (0.1V per bit) se CAN1 non attivo
        if (!isRunning(0)) {
            float v = static_cast<float>(d[2]) * 0.1f;
            if (v >= 7.0f && v <= 18.0f) {
                store.set(data::Metric::BatteryVolts, v);
            }
        }
    }
}

void CanManager::taskTrampolineCan1(void* arg) {
    static_cast<CanManager*>(arg)->runCan1();
}

void CanManager::taskTrampolineCan2(void* arg) {
    static_cast<CanManager*>(arg)->runCan2();
}

void CanManager::runCan1() {
    ESP_LOGI(TAG, "CAN 1 (C-CAN Powertrain 500k) task avviato");

    const auto& s = settings::current();
    const gpio_num_t tx_pin = static_cast<gpio_num_t>(s.can1_tx_pin);
    const gpio_num_t rx_pin = static_cast<gpio_num_t>(s.can1_rx_pin);
    const twai_mode_t mode  = s.can_listen_only ? TWAI_MODE_LISTEN_ONLY : TWAI_MODE_NORMAL;

    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT_V2(0, tx_pin, rx_pin, mode);
    g_config.rx_queue_len = 64;
    g_config.tx_queue_len = 16;
    g_config.alerts_enabled = TWAI_ALERT_BUS_OFF | TWAI_ALERT_BUS_RECOVERED |
                              TWAI_ALERT_ERR_PASS | TWAI_ALERT_ABOVE_ERR_WARN |
                              TWAI_ALERT_RX_QUEUE_FULL;

    twai_timing_config_t t_config = getTimingConfig(s.can1_baud_rate);
    twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    esp_err_t err = twai_driver_install_v2(&g_config, &t_config, &f_config, &twai_handles_[0]);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "CAN 1 twai_driver_install_v2 fallito: %s (TX=%d, RX=%d)", esp_err_to_name(err), tx_pin, rx_pin);
        bus_[0].last_error = "driver install fallito (" + std::string(esp_err_to_name(err)) + ")";
        setBusState(0, CanState::Error);
        tasks_[0] = nullptr;
        vTaskDelete(nullptr);
        return;
    }

    err = twai_start_v2(twai_handles_[0]);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "CAN 1 twai_start_v2 fallito: %s", esp_err_to_name(err));
        bus_[0].last_error = "twai start fallito (" + std::string(esp_err_to_name(err)) + ")";
        twai_driver_uninstall_v2(twai_handles_[0]);
        twai_handles_[0] = nullptr;
        setBusState(0, CanState::Error);
        tasks_[0] = nullptr;
        vTaskDelete(nullptr);
        return;
    }

    setBusState(0, CanState::Running);
    ESP_LOGI(TAG, "CAN 1 attivo su TX=GPIO%d, RX=GPIO%d a %lu bps", tx_pin, rx_pin, static_cast<unsigned long>(s.can1_baud_rate));

    int64_t last_fps_time_us = esp_timer_get_time();
    uint32_t last_rx_count = 0;
    last_query_time_us_ = esp_timer_get_time();
    int64_t last_bus_off_time_us = 0;
    int64_t last_warn_log_us = 0;

    while (!stop_requested_[0]) {
        // 1. Gestione Alerts e Bus-Off
        uint32_t alerts = 0;
        if (twai_read_alerts_v2(twai_handles_[0], &alerts, 0) == ESP_OK && alerts != 0) {
            if (alerts & TWAI_ALERT_BUS_OFF) {
                const int64_t b_now = esp_timer_get_time();
                last_bus_off_time_us = b_now;
                if ((b_now - last_warn_log_us) > 3000000) {
                    last_warn_log_us = b_now;
                    ESP_LOGW(TAG, "CAN 1 Bus-Off (linea aperta o modulo non connesso). Avvio recupero...");
                }
                setBusState(0, CanState::BusOff);
                bus_[0].err_count.fetch_add(1, std::memory_order_relaxed);
                twai_initiate_recovery_v2(twai_handles_[0]);
                setBusState(0, CanState::Recovering);
            }
            if (alerts & TWAI_ALERT_BUS_RECOVERED) {
                ESP_LOGI(TAG, "CAN 1 Bus recuperato! Riavvio interfaccia...");
                twai_start_v2(twai_handles_[0]);
                setBusState(0, CanState::Running);
            }
            if (alerts & (TWAI_ALERT_ERR_PASS | TWAI_ALERT_ABOVE_ERR_WARN)) {
                bus_[0].err_count.fetch_add(1, std::memory_order_relaxed);
            }
        }

        // 2. Ricezione frame CAN (non-bloccante con timeout 10ms)
        twai_message_t rx_msg;
        esp_err_t rx_err = twai_receive_v2(twai_handles_[0], &rx_msg, pdMS_TO_TICKS(10));
        if (rx_err == ESP_OK) {
            handleReceivedMessageCan1(rx_msg);
        }

        // 3. Polling attivo OBD-over-CAN (ogni 25ms se non in listen-only e dopo almeno 2.5s dall'ultimo bus-off)
        const int64_t now_us = esp_timer_get_time();
        if (bus_[0].state.load() == CanState::Running && (now_us - last_bus_off_time_us) >= 2500000) {
            if ((now_us - last_query_time_us_) >= 25000) { // 25 ms (~40 Hz)
                last_query_time_us_ = now_us;
                sendNextObdQuery();
            }
        }

        // 4. Calcolo Throughput FPS (ogni 1.0 s)
        if ((now_us - last_fps_time_us) >= 1000000) {
            const uint32_t current_rx = bus_[0].rx_count.load(std::memory_order_relaxed);
            const float fps = static_cast<float>(current_rx - last_rx_count) * 1000000.0f / static_cast<float>(now_us - last_fps_time_us);
            bus_[0].rx_fps.store(fps, std::memory_order_relaxed);
            last_rx_count = current_rx;
            last_fps_time_us = now_us;
        }
    }

    twai_stop_v2(twai_handles_[0]);
    twai_driver_uninstall_v2(twai_handles_[0]);
    twai_handles_[0] = nullptr;
    setBusState(0, CanState::Stopped);
    tasks_[0] = nullptr;
    vTaskDelete(nullptr);
}

void CanManager::runCan2() {
    ESP_LOGI(TAG, "CAN 2 (B-CAN Body 50k) task avviato");

    const auto& s = settings::current();
    const gpio_num_t tx_pin = static_cast<gpio_num_t>(s.can2_tx_pin);
    const gpio_num_t rx_pin = static_cast<gpio_num_t>(s.can2_rx_pin);
    // B-CAN normalmente lavora in sola ricezione passiva per non interferire con i nodi comfort
    const twai_mode_t mode = TWAI_MODE_LISTEN_ONLY;

    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT_V2(1, tx_pin, rx_pin, mode);
    g_config.rx_queue_len = 64;
    g_config.tx_queue_len = 16;
    g_config.alerts_enabled = TWAI_ALERT_BUS_OFF | TWAI_ALERT_BUS_RECOVERED |
                              TWAI_ALERT_ERR_PASS | TWAI_ALERT_ABOVE_ERR_WARN |
                              TWAI_ALERT_RX_QUEUE_FULL;

    twai_timing_config_t t_config = getTimingConfig(s.can2_baud_rate);
    twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    esp_err_t err = twai_driver_install_v2(&g_config, &t_config, &f_config, &twai_handles_[1]);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "CAN 2 twai_driver_install_v2 fallito: %s (TX=%d, RX=%d)", esp_err_to_name(err), tx_pin, rx_pin);
        bus_[1].last_error = "driver install fallito (" + std::string(esp_err_to_name(err)) + ")";
        setBusState(1, CanState::Error);
        tasks_[1] = nullptr;
        vTaskDelete(nullptr);
        return;
    }

    err = twai_start_v2(twai_handles_[1]);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "CAN 2 twai_start_v2 fallito: %s", esp_err_to_name(err));
        bus_[1].last_error = "twai start fallito (" + std::string(esp_err_to_name(err)) + ")";
        twai_driver_uninstall_v2(twai_handles_[1]);
        twai_handles_[1] = nullptr;
        setBusState(1, CanState::Error);
        tasks_[1] = nullptr;
        vTaskDelete(nullptr);
        return;
    }

    setBusState(1, CanState::Running);
    ESP_LOGI(TAG, "CAN 2 attivo su TX=GPIO%d, RX=GPIO%d a %lu bps", tx_pin, rx_pin, static_cast<unsigned long>(s.can2_baud_rate));

    int64_t last_fps_time_us = esp_timer_get_time();
    uint32_t last_rx_count = 0;
    int64_t last_warn_log_us = 0;

    while (!stop_requested_[1]) {
        // 1. Gestione Alerts e Bus-Off
        uint32_t alerts = 0;
        if (twai_read_alerts_v2(twai_handles_[1], &alerts, 0) == ESP_OK && alerts != 0) {
            if (alerts & TWAI_ALERT_BUS_OFF) {
                const int64_t b_now = esp_timer_get_time();
                if ((b_now - last_warn_log_us) > 3000000) {
                    last_warn_log_us = b_now;
                    ESP_LOGW(TAG, "CAN 2 Bus-Off (linea aperta o modulo non connesso). Avvio recupero...");
                }
                setBusState(1, CanState::BusOff);
                bus_[1].err_count.fetch_add(1, std::memory_order_relaxed);
                twai_initiate_recovery_v2(twai_handles_[1]);
                setBusState(1, CanState::Recovering);
            }
            if (alerts & TWAI_ALERT_BUS_RECOVERED) {
                ESP_LOGI(TAG, "CAN 2 Bus recuperato! Riavvio interfaccia...");
                twai_start_v2(twai_handles_[1]);
                setBusState(1, CanState::Running);
            }
            if (alerts & (TWAI_ALERT_ERR_PASS | TWAI_ALERT_ABOVE_ERR_WARN)) {
                bus_[1].err_count.fetch_add(1, std::memory_order_relaxed);
            }
        }

        // 2. Ricezione frame CAN (non-bloccante con timeout 20ms)
        twai_message_t rx_msg;
        esp_err_t rx_err = twai_receive_v2(twai_handles_[1], &rx_msg, pdMS_TO_TICKS(20));
        if (rx_err == ESP_OK) {
            handleReceivedMessageCan2(rx_msg);
        }

        // 3. Calcolo Throughput FPS (ogni 1.0 s)
        const int64_t now_us = esp_timer_get_time();
        if ((now_us - last_fps_time_us) >= 1000000) {
            const uint32_t current_rx = bus_[1].rx_count.load(std::memory_order_relaxed);
            const float fps = static_cast<float>(current_rx - last_rx_count) * 1000000.0f / static_cast<float>(now_us - last_fps_time_us);
            bus_[1].rx_fps.store(fps, std::memory_order_relaxed);
            last_rx_count = current_rx;
            last_fps_time_us = now_us;
        }
    }

    twai_stop_v2(twai_handles_[1]);
    twai_driver_uninstall_v2(twai_handles_[1]);
    twai_handles_[1] = nullptr;
    setBusState(1, CanState::Stopped);
    tasks_[1] = nullptr;
    vTaskDelete(nullptr);
}

CanManager& can() {
    static CanManager s_instance;
    return s_instance;
}

}  // namespace abarth::can
