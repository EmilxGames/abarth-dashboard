#pragma once

/**
 * @file ble_stream.h
 * @brief Wrapper BLE che espone una semplice interfaccia stream.
 *
 * L'ESP32-P4 tramite il co-processore ESP32-C6 supporta solo Bluetooth LE
 * (niente Bluetooth Classic / SPP). I dongle come vLinker FD, OBDLink CX e
 * la maggior parte dei cloni cinesi ELM327 "compatibili iPhone" espongono
 * un servizio GATT 0xFFF0 con due caratteristiche:
 *
 *   - 0xFFF1 : NOTIFY (dati da dongle -> host)
 *   - 0xFFF2 : WRITE  (comandi AT/OBD da host -> dongle)
 *
 * Nota: Arduino-ESP32 3.1.1 al momento non include i symboli NimBLE per
 * ESP32-P4 (il BT va via esp_hosted -> ESP32-C6). Finche' non e' pronto,
 * compilando con `-DABARTH_HAS_BLE=0` il backend diventa uno stub che
 * finge un errore di connessione: l'UI parte e mostra "Dongle non trovato",
 * ma non si interroga l'auto. Vedi README sezione "Stato del supporto BLE".
 */



#include <atomic>
#include <string>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/stream_buffer.h"

#include "obd/istream.h"

#ifndef ABARTH_HAS_BLE
#define ABARTH_HAS_BLE 1
#endif

#if ABARTH_HAS_BLE
#include <NimBLEDevice.h>
#endif

namespace abarth::obd {

struct BleStreamConfig {
    const char* device_name   = "vLinker FD";
    const char* service_uuid  = "FFF0";
    const char* notify_uuid   = "FFF1";
    const char* write_uuid    = "FFF2";
    uint32_t    scan_time_s   = 10;
    size_t      rx_buffer_cap = 4096;   ///< buffer FIFO interno
    std::string target_mac    = "";     ///< Indirizzo MAC specifico del dongle BLE (se vuoto usa la ricerca automatica)
};

class BleStream : public IStream {
public:
    BleStream();
    ~BleStream() override;

    BleStream(const BleStream&)            = delete;
    BleStream& operator=(const BleStream&) = delete;

    /// Inizializza lo stack BLE (da chiamare una sola volta in setup()).
    bool begin(const BleStreamConfig& cfg);

    /// Avvia scansione + connessione al primo dispositivo che matcha il nome
    /// o il service UUID configurato. Bloccante, ritorna true se connesso.
    bool connect() override;

    /// Avvia una scansione per restituire la lista di dispositivi trovati (Nome - MAC).
    /// Bloccante, ci mette scan_time_s secondi.
    std::vector<std::string> scanDevices(uint32_t scan_time_s = 5);

    /// Disconnette e libera il client.
    void disconnect() override;

    bool isConnected() const override { return connected_.load(); }

    /// Scrive un buffer grezzo verso la caratteristica di write. Esegue
    /// fragmentazione se serve (MTU < size).
    size_t write(const uint8_t* data, size_t len) override;
    size_t write(const char* str) override;

    /// Legge fino a trovare `terminator` (tipicamente '>' per ELM327) o
    /// scadenza timeout. Ritorna quanti byte sono stati scritti in `out`.
    /// La stringa non include il terminatore.
    size_t readUntil(char terminator, char* out, size_t out_max, uint32_t timeout_ms) override;

    /// Svuota il buffer interno.
    void flushRx() override;

    const BleStreamConfig& config() const { return cfg_; }

#if ABARTH_HAS_BLE
private:
    /// Callback invocato da NimBLE sulle notifiche di 0xFFF1.
    void onNotify(NimBLERemoteCharacteristic* chr, uint8_t* data, size_t len, bool /*isNotify*/);

    class ClientCallbacks;
    friend class ClientCallbacks;

    NimBLEClient*                client_          = nullptr;
    NimBLERemoteCharacteristic*  notify_char_     = nullptr;
    NimBLERemoteCharacteristic*  write_char_      = nullptr;
#endif

private:
    BleStreamConfig              cfg_{};
    StreamBufferHandle_t         rx_buf_          = nullptr;
    std::atomic<bool>            connected_{false};
    std::atomic<bool>            initialized_{false};
};

}  // namespace abarth::obd
