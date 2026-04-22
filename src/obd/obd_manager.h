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

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "obd/ble_stream.h"
#include "obd/elm327.h"

namespace abarth::obd {

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

private:
    static void taskTrampoline(void* arg);
    void        run();

    /// Esegue un passo del polling (ritorna true se la connessione e' sana).
    bool pollOnce();

    void setState(ObdState s);

    BleStream           ble_;
    Elm327              elm_;
    std::atomic<ObdState> state_{ObdState::Disconnected};
    std::atomic<uint32_t> success_count_{0};
    std::atomic<uint32_t> fail_count_{0};
    std::string         last_error_;
    TaskHandle_t        task_ = nullptr;
};

ObdManager& obd();

}  // namespace abarth::obd
