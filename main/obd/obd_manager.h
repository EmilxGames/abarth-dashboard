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

#include <memory>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

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

    /// Messaggio di stato human-readable (errori). Copia thread-safe.
    std::string lastError() const;

    // Tutte le operazioni su stream ed ELM327 vengono eseguite dal task OBD,
    // che le riceve da una coda (revisione C5): nessun altro task tocca lo stream.

    /// Forza la connessione BLE manuale (asincrono)
    void connectBLE(const std::string& mac = "");

    /// Forza la connessione WiFi manuale (asincrono)
    void connectWiFi(const std::string& ip = "192.168.0.10", uint16_t port = 35000);

    /// Disconnette e dimentica il device (asincrono)
    void forgetDevice();

    /// Ferma il servizio (es. passaggio a CAN diretto) SENZA cancellare
    /// l'associazione salvata (asincrono)
    void stopService();

    /// Legge i codici errore (DTC Mode 03 + Mode 07) dalla centralina.
    /// Bloccante (fino a ~15 s): NON chiamarla dal task LVGL.
    bool readDtcCodes(std::vector<DtcItem>& out_codes, std::string& out_status_msg);

    /// Cancella i codici errore (DTC Mode 04) dalla centralina e resetta la spia MIL.
    /// Rifiuta l'operazione se dtcClearAllowed() e' false. Bloccante: NON dal task LVGL.
    bool clearDtcCodes(std::string& out_status_msg);

    /// Mode 04 consentito solo a motore spento e auto ferma: RPM = 0 e
    /// velocita' = 0 letti dalla centralina negli ultimi 2 s.
    bool dtcClearAllowed(std::string& out_reason) const;

private:
    struct DtcRequest;
    struct Command;

    static void taskTrampoline(void* arg);
    void        run();

    bool ensureQueue();
    void post(Command* cmd);
    /// Esegue i comandi in coda; attende al massimo `wait` il primo.
    void processCommands(TickType_t wait);
    void execute(Command& cmd);

    void connectBleImpl(const std::string& mac);
    void connectWifiImpl(const std::string& ip, uint16_t port);
    void forgetImpl();
    bool readDtcImpl(std::vector<DtcItem>& out_codes, std::string& out_status_msg);
    bool clearDtcImpl(std::string& out_status_msg);
    void setLastError(const std::string& e);

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
    std::string         last_error_;          ///< protetto da err_mtx_
    SemaphoreHandle_t   err_mtx_ = nullptr;
    QueueHandle_t       cmd_q_ = nullptr;
    TaskHandle_t        task_ = nullptr;
    bool                manual_connection_req_ = false;  ///< solo task OBD
};

ObdManager& obd();

}  // namespace abarth::obd
