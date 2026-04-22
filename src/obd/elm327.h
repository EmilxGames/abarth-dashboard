#pragma once

/**
 * @file elm327.h
 * @brief Client ELM327 minimale via BleStream.
 *
 * Implementa il sottoinsieme dei comandi AT necessari per inizializzare il
 * dongle e inviare richieste OBD (mode 01 e mode 22). Risponde in stringhe
 * ASCII (senza i ritorni carrello) che la classe chiamante parse-era.
 */

#include <cstddef>
#include <cstdint>
#include <string>

#include "obd/ble_stream.h"

namespace abarth::obd {

struct ElmResponse {
    bool        ok        = false;
    std::string raw;        ///< risposta grezza senza prompt '>'
    std::string error;      ///< se !ok
};

class Elm327 {
public:
    explicit Elm327(BleStream& stream);

    /// Esegue la sequenza di inizializzazione (ATZ, ATE0, ATL0, ATS0, ATSP0,
    /// ATAT1). Ritorna true se il dongle risponde correttamente.
    bool init(uint32_t timeout_ms = 3000);

    /// Invia un comando grezzo (es. "0105" o "22706E" o "AT RV") e attende
    /// il prompt '>'.
    ElmResponse command(const std::string& cmd, uint32_t timeout_ms);

    /// Ritorna la tensione batteria letta internamente dal dongle (AT RV).
    /// Ritorna NaN se non disponibile.
    float readBatteryVoltage(uint32_t timeout_ms = 1500);

private:
    BleStream& stream_;
};

}  // namespace abarth::obd
