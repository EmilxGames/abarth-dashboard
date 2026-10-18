#pragma once

/**
 * @file obd_manager.h
 * @brief Task FreeRTOS che gestisce connessione e polling dell'auto.
 *
 * Stati:
 *   DISCONNECTED -> SCANNING -> CONNECTING -> INITIALIZING -> POLLING
 *   Da qualunque stato un errore porta a DISCONNECTED (con backoff).
 */

#include <atomic>
#include <string>
#include <vector>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "obd/ble_stream.h"
#include "obd/wifi_stream.h"
#include "obd/elm327.h"

namespace abarth::obd {

struct DtcItem {
    std::string code;         // e.g. "P0300"
    std::string description;  // e.g. "Mancata accensione cilindri casuale/multiplo"
};

enum class ObdState : uint8_t {
    Disconnected = 0,
    Scanning,
    Connecting,
    Initializing,
    Polling,
    Error,
};

const char* stateName(ObdState s);

class ObdManager {
public:
    ObdManager();

    /// Parte il task in background. Chiamare una sola volta.
    bool start();

    /// Stato corrente (lock-free).
    ObdState state() const { return state_.load(); }

    /// Numero di PID letti con successo dall'avvio.
    uint32_t successfulReads() const { return success_count_.load(); }
    uint32_t failedReads() const     { return fail_count_.load(); }

    /// Messaggio di stato human-readable (errori).
    const std::string& lastError() const { return last_error_; }

    /// Forza la connessione BLE manuale
    void connectBLE(const std::string& mac = "");

    /// Forza la connessione WiFi manuale
    void connectWiFi(const std::string& ip = "192.168.0.10", uint16_t port = 35000);

    /// Disconnette e dimentica il device
    void forgetDevice();

    /// Legge i codici errore (DTC Mode 03 + Mode 07) dalla centralina.
    bool readDtcCodes(std::vector<DtcItem>& out_codes, std::string& out_status_msg);

    /// Cancella i codici errore (DTC Mode 04) dalla centralina e resetta la spia MIL.
    bool clearDtcCodes(std::string& out_status_msg);

private:
    static void taskTrampoline(void* arg);
    void        run();

    /// Esegue un passo del polling (ritorna true se la connessione e' sana).
    bool pollOnce();

    void setState(ObdState s);

    BleStream           ble_;
    WifiStream          wifi_;
    IStream*            active_stream_ = nullptr;
    Elm327              elm_;
    std::atomic<ObdState> state_{ObdState::Disconnected};
    std::atomic<uint32_t> success_count_{0};
    std::atomic<uint32_t> fail_count_{0};
    std::string         last_error_;
    TaskHandle_t        task_ = nullptr;
    bool                manual_connection_req_ = false;
};

ObdManager& obd();

}  // namespace abarth::obd
