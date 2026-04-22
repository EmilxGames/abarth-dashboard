#pragma once

/**
 * @file ble_stream.h
 * @brief Wrapper BLE (NimBLE) che espone una semplice interfaccia stream.
 *
 * L'ESP32-P4 tramite il co-processore ESP32-C6 supporta solo Bluetooth LE
 * (niente Bluetooth Classic / SPP). I dongle come vLinker FD, OBDLink CX e
 * la maggior parte dei cloni cinesi ELM327 "compatibili iPhone" espongono
 * un servizio GATT 0xFFF0 con due caratteristiche:
 *
 *   - 0xFFF1 : NOTIFY (dati da dongle -> host)
 *   - 0xFFF2 : WRITE  (comandi AT/OBD da host -> dongle)
 *
 * Questa classe nasconde la complessita' di NimBLE: dopo `connect()` si
 * possono usare `write()` / `readLine()` come fosse una seriale.
 */

#include <Arduino.h>
#include <NimBLEDevice.h>

#include <atomic>
#include <string>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/stream_buffer.h"

namespace abarth::obd {

struct BleStreamConfig {
    const char* device_name   = "vLinker FD";
    const char* service_uuid  = "FFF0";
    const char* notify_uuid   = "FFF1";
    const char* write_uuid    = "FFF2";
    uint32_t    scan_time_s   = 10;
    size_t      rx_buffer_cap = 4096;   ///< buffer FIFO interno
};

class BleStream {
public:
    BleStream();
    ~BleStream();

    BleStream(const BleStream&)            = delete;
    BleStream& operator=(const BleStream&) = delete;

    /// Inizializza lo stack BLE (da chiamare una sola volta in setup()).
    bool begin(const BleStreamConfig& cfg);

    /// Avvia scansione + connessione al primo dispositivo che matcha il nome
    /// o il service UUID configurato. Bloccante, ritorna true se connesso.
    bool connect();

    /// Disconnette e libera il client.
    void disconnect();

    bool isConnected() const { return connected_.load(); }

    /// Scrive un buffer grezzo verso la caratteristica di write. Esegue
    /// fragmentazione se serve (MTU < size).
    size_t write(const uint8_t* data, size_t len);
    size_t write(const char* str);

    /// Legge fino a trovare `terminator` (tipicamente '>' per ELM327) o
    /// scadenza timeout. Ritorna quanti byte sono stati scritti in `out`.
    /// La stringa non include il terminatore.
    size_t readUntil(char terminator, char* out, size_t out_max, uint32_t timeout_ms);

    /// Svuota il buffer interno.
    void flushRx();

    const BleStreamConfig& config() const { return cfg_; }

private:
    /// Callback invocato da NimBLE sulle notifiche di 0xFFF1.
    void onNotify(NimBLERemoteCharacteristic* chr, uint8_t* data, size_t len, bool /*isNotify*/);

    class ClientCallbacks;
    friend class ClientCallbacks;

    BleStreamConfig              cfg_{};
    NimBLEClient*                client_          = nullptr;
    NimBLERemoteCharacteristic*  notify_char_     = nullptr;
    NimBLERemoteCharacteristic*  write_char_      = nullptr;
    StreamBufferHandle_t         rx_buf_          = nullptr;
    std::atomic<bool>            connected_{false};
    std::atomic<bool>            initialized_{false};
};

}  // namespace abarth::obd
