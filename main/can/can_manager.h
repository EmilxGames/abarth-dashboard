#pragma once

/**
 * @file can_manager.h
 * @brief Gestore nativo interfaccia Dual CAN-Bus (TWAI) per ESP32-P4.
 *
 * Supporta:
 * - CAN 1: C-CAN Powertrain (500 kbps, TWAI0) - Ricezione passiva frames motore Abarth
 *          + interrogazione attiva ad alta frequenza OBD-over-CAN (ISO 15765-4 su 0x7DF/0x7E8)
 * - CAN 2: B-CAN Body (50 kbps, TWAI1) - Ricezione passiva frames carrozzeria (luci, porte,
 *          odometro, tasti al volante, freno a mano)
 * - Compatibile con moduli WeAct Studio ISOCANFD V1 (isolamento 2.5 kV CA-IS2062A)
 * - Controllo indipendente per attivare/disattivare CAN 1, CAN 2 o entrambi al volo
 */

#include <atomic>
#include <cstdint>
#include <string>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/twai.h"
#include "data/data_store.h"

namespace abarth::can {

enum class CanState : uint8_t {
    Stopped = 0,
    Starting,
    Running,
    BusOff,
    Recovering,
    Error,
};

const char* canStateName(CanState s);

struct BusStats {
    std::atomic<CanState> state{CanState::Stopped};
    std::atomic<uint32_t> rx_count{0};
    std::atomic<uint32_t> tx_count{0};
    std::atomic<uint32_t> err_count{0};
    std::atomic<float>    rx_fps{0.0f};
    std::string           last_error;
};

class CanManager {
public:
    CanManager();
    ~CanManager();

    CanManager(const CanManager&) = delete;
    CanManager& operator=(const CanManager&) = delete;

    /// Avvia i canali CAN abilitati (CAN 1 e/o CAN 2)
    bool start();

    /// Arresta tutti i driver e i task CAN
    void stop();

    /// Avvia o arresta un singolo bus (0 = CAN1 C-CAN, 1 = CAN2 B-CAN)
    bool startBus(uint8_t bus_idx);
    void stopBus(uint8_t bus_idx);

    /// Riavvia tutti i bus abilitati
    bool restart();

    /// Riavvia un singolo bus (0 = CAN1, 1 = CAN2)
    bool restartBus(uint8_t bus_idx);

    /// Stato operativo corrente del bus (0 o 1)
    CanState state(uint8_t bus_idx = 0) const;
    bool isRunning(uint8_t bus_idx = 0) const;

    /// Statistiche per singolo bus
    uint32_t rxFrames(uint8_t bus_idx = 0) const;
    uint32_t txFrames(uint8_t bus_idx = 0) const;
    uint32_t busErrors(uint8_t bus_idx = 0) const;
    float    rxRateFps(uint8_t bus_idx = 0) const;
    const std::string& lastError(uint8_t bus_idx = 0) const;

    /// Statistiche globali aggregate
    uint32_t totalRxFrames() const { return rxFrames(0) + rxFrames(1); }
    uint32_t totalErrors() const   { return busErrors(0) + busErrors(1); }

    /// Trasmette un frame CAN grezzo sul bus selezionato (0 = CAN1, 1 = CAN2)
    bool transmit(uint8_t bus_idx, uint32_t id, bool extd, const uint8_t* data, uint8_t len);

private:
    static void taskTrampolineCan1(void* arg);
    static void taskTrampolineCan2(void* arg);
    void runCan1();
    void runCan2();

    void setBusState(uint8_t bus_idx, CanState s);
    void handleReceivedMessageCan1(const twai_message_t& msg);
    void handleReceivedMessageCan2(const twai_message_t& msg);
    void sendNextObdQuery();

    BusStats      bus_[2];
    twai_handle_t twai_handles_[2] = {nullptr, nullptr};
    TaskHandle_t  tasks_[2]        = {nullptr, nullptr};
    bool          stop_requested_[2] = {false, false};

    // Gestione query OBD-over-CAN (su CAN 1 C-CAN)
    uint8_t current_pid_index_  = 0;
    int64_t last_query_time_us_ = 0;
};

CanManager& can();

}  // namespace abarth::can
