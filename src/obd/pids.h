#pragma once

/**
 * @file pids.h
 * @brief Definizione dei PID letti dall'auto e decoder delle risposte.
 *
 * Suddivisione:
 * - PID "standard" OBDII Mode 01 (supportati da qualunque auto dal 2001+)
 * - PID "custom" Mode 22 specifici per centralina Fiat/Abarth (necessari
 *   per temperatura olio, boost reale, pressione olio, ecc.)
 *
 * I valori numerici dei PID Mode 22 per Abarth 595 provengono da:
 * - ScanGauge X-Gauge "Fiat 500 Abarth"
 *   https://scangauge.com/x-gauge-commands/fiat-500-abarth
 * - Car Scanner (app Android/iOS) community-shared PID list
 * - Alfa/FIAT forum (community reverse engineering)
 *
 * IMPORTANTE: i PID Mode 22 variano per millesimo/centralina. Se qualcuno
 * non risponde sulla tua auto, modifica `kAbarthCustomPids` qui sotto.
 */

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string_view>

#include "data/data_store.h"

namespace abarth::obd {

struct PidResponse;

/// Funzione che estrae il valore numerico dalla risposta ELM (bytes in HEX
/// ASCII, gia' ripulita da spazi e prompt). Ritorna true se il parse e' ok
/// e popola `out`. Per valori invalidi o formati inattesi, ritorna false.
using PidDecoder = std::function<bool(const uint8_t* data, size_t n, float& out)>;

struct PidSpec {
    std::string_view name;           ///< nome leggibile (IT)
    data::Metric     metric;         ///< dove salvare il valore
    std::string_view unit;           ///< unita' di misura (es. "°C")
    std::string_view request;        ///< testo comando da inviare (es. "010C")
    uint8_t          response_mode;  ///< byte di risposta atteso (es. 0x41 per mode 01)
    uint16_t         pid_id;         ///< PID numerico (per mode 01: 0..0xFF, mode 22: 0x0000..0xFFFF)
    uint8_t          pid_bytes;      ///< lunghezza PID nella risposta (1 per mode 01, 2 per mode 22)
    uint8_t          data_bytes;     ///< byte di dato attesi dopo il PID
    uint32_t         period_ms;      ///< intervallo desiderato (0 = "appena possibile")
    PidDecoder       decode;
};

/* === PID standard OBDII (Mode 01) ---------------------------------------- */

extern const PidSpec kStandardPids[];
extern const size_t  kStandardPidsCount;

/* === PID custom Abarth/Fiat (Mode 22) ------------------------------------ */

extern const PidSpec kAbarthCustomPids[];
extern const size_t  kAbarthCustomPidsCount;

/* === Utility di parsing -------------------------------------------------- */

/// Converte una stringa hex (es. "41 0C 0F A0" o "410C0FA0") in array di byte.
/// Se la prima coppia corrisponde al mode atteso, viene verificato. Gli header
/// CAN ("7E8 03 41 0C ...") vengono rimossi automaticamente.
/// @return numero di byte decodificati, 0 su errore.
size_t parseHexPayload(std::string_view hex, uint8_t* out, size_t out_max);

/// Estrae la porzione dati dalla risposta, verificando che mode e pid_id
/// siano quelli attesi. Popola `data` (subset di `raw`) e ritorna true.
bool extractPidData(const uint8_t* raw, size_t raw_len,
                    const PidSpec& spec, const uint8_t*& data_out);

}  // namespace abarth::obd
