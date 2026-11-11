#include "obd/pids.h"

#include <cctype>
#include <cstring>
#include <cstdlib>
#include <string>

namespace abarth::obd {

namespace {

int hex_nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    return -1;
}

/* --- Decoder helper ------------------------------------------------------ */

bool dec_rpm(const uint8_t* d, size_t n, float& out) {
    if (n < 2) return false;
    out = ((d[0] * 256) + d[1]) / 4.0f;
    return true;
}

bool dec_speed(const uint8_t* d, size_t n, float& out) {
    if (n < 1) return false;
    out = d[0];
    return true;
}

bool dec_load_or_throttle(const uint8_t* d, size_t n, float& out) {
    if (n < 1) return false;
    out = d[0] * 100.0f / 255.0f;
    return true;
}

bool dec_temp_c_minus_40(const uint8_t* d, size_t n, float& out) {
    if (n < 1) return false;
    out = static_cast<int>(d[0]) - 40;
    return true;
}

[[maybe_unused]] bool dec_signed_temp_c(const uint8_t* d, size_t n, float& out) {
    if (n < 1) return false;
    int8_t s = static_cast<int8_t>(d[0]);
    out = s;
    return true;
}

bool dec_map_kpa(const uint8_t* d, size_t n, float& out) {
    if (n < 1) return false;
    out = d[0];   // kPa assoluti
    return true;
}

bool dec_control_module_voltage(const uint8_t* d, size_t n, float& out) {
    if (n < 2) return false;
    out = ((d[0] * 256) + d[1]) / 1000.0f;
    return true;
}

bool dec_egt_k(const uint8_t* d, size_t n, float& out) {
    // PID 6B (0x6B) Exhaust Gas Temperature bank 1. Layout:
    //   byte0 = supported sensors mask
    //   byte1/2 = sensor1 temp = (A*256+B)/10 - 40 [°C]
    //   ... fino a 4 sensori
    if (n < 3) return false;
    out = ((d[1] * 256) + d[2]) / 10.0f - 40.0f;
    return true;
}

bool dec_lambda_afr(const uint8_t* d, size_t n, float& out) {
    // PID 34 (O2S1_WR_lambda). (A*256+B)/32768 = lambda. AFR = lambda * 14.7
    if (n < 2) return false;
    float lambda = ((d[0] * 256) + d[1]) / 32768.0f;
    out = lambda * 14.7f;
    return true;
}

/* --- Decoder Mode 22 Fiat/Abarth ---------------------------------------- */

// Temperatura olio motore (PID 0x706E): byte singolo, formula ScanGauge:
// degF = raw*9/5 - 32  =>  raw e' direttamente °C (senza offset).
bool dec_fiat_oil_temp(const uint8_t* d, size_t n, float& out) {
    if (n < 1) return false;
    out = d[0];
    return true;
}

// Pressione olio (PID 0x????): placeholder "raw in bar*10". Da confermare
// sull'auto: se i valori risultano strani, tarare la formula qui.
bool dec_fiat_oil_pressure(const uint8_t* d, size_t n, float& out) {
    if (n < 1) return false;
    out = d[0] / 10.0f;
    return true;
}

// Boost reale (pressione sovralimentazione, relativa all'atmosfera, in bar).
// Formula tipica FIAT MultiAir: bar = (raw16 / 1000) - 1.0
bool dec_fiat_boost_rel_bar(const uint8_t* d, size_t n, float& out) {
    if (n < 2) return false;
    uint16_t raw = (d[0] << 8) | d[1];
    out = (raw / 1000.0f) - 1.0f;
    return true;
}

/* --- Decoder carburante --------------------------------------------------- */

// Livello carburante % (PID 012F): 1 byte, formula 100*raw/255.
bool dec_fuel_level_pct(const uint8_t* d, size_t n, float& out) {
    if (n < 1) return false;
    out = d[0] * 100.0f / 255.0f;
    return true;
}

// Pressione rail carburante, manometrica (PID 0123, SAE J1979): 10 * (256A+B) kPa.
bool dec_fuel_pressure_kpa(const uint8_t* d, size_t n, float& out) {
    if (n < 2) return false;
    const uint16_t raw = (d[0] << 8) | d[1];
    out = raw * 10.0f;
    return true;
}

// Pressione rail carburante assoluta (PID 0159, SAE J1979): 10 * (256A+B) kPa.
// (0.079 * raw e' la formula del PID 0122, non di questo.)
bool dec_fuel_rail_pressure_kpa(const uint8_t* d, size_t n, float& out) {
    if (n < 2) return false;
    const uint16_t raw = (d[0] << 8) | d[1];
    out = raw * 10.0f;
    return true;
}

// Portata carburante L/h (PID 015E): 2 byte, formula raw/20.
bool dec_fuel_rate_lh(const uint8_t* d, size_t n, float& out) {
    if (n < 2) return false;
    uint16_t raw = (d[0] << 8) | d[1];
    out = raw / 20.0f;
    return true;
}

// Temperatura carburante °C (PID 0126, non sempre supportato): raw - 40.
bool dec_fuel_temp_c(const uint8_t* d, size_t n, float& out) {
    if (n < 1) return false;
    out = static_cast<int>(d[0]) - 40;
    return true;
}

// Anticipo iniezione ° (PID 015D, SAE J1979): (256A+B)/128 - 210 gradi,
// con valore SENZA segno (int16_t andava in overflow sopra 32767).
bool dec_injection_timing(const uint8_t* d, size_t n, float& out) {
    if (n < 2) return false;
    const uint16_t raw = (d[0] << 8) | d[1];
    out = raw / 128.0f - 210.0f;
    return true;
}

// Percentuale etanolo nel carburante (PID 0152, veicoli Flex Fuel): raw*100/255.
bool dec_ethanol_pct(const uint8_t* d, size_t n, float& out) {
    if (n < 1) return false;
    out = d[0] * 100.0f / 255.0f;
    return true;
}

// Distanza dall'ultimo reset DTC in km (PID 0131): 2 byte raw.
bool dec_distance_since_clear(const uint8_t* d, size_t n, float& out) {
    if (n < 2) return false;
    out = (d[0] * 256) + d[1];
    return true;
}

}  // namespace

/* === Tabelle PID ========================================================= */

const PidSpec kStandardPids[] = {
    {
        "Giri motore", data::Metric::Rpm, "RPM",
        "010C", 0x41, 0x0C, 1, 2, 0,
        dec_rpm, /*is_priority=*/true,
    },
    {
        "Velocita'", data::Metric::Speed, "km/h",
        "010D", 0x41, 0x0D, 1, 1, 0,
        dec_speed, /*is_priority=*/true,
    },
    {
        "Carico motore", data::Metric::EngineLoad, "%",
        "0104", 0x41, 0x04, 1, 1, 0,
        dec_load_or_throttle, /*is_priority=*/true,
    },
    {
        "Acceleratore", data::Metric::Throttle, "%",
        "0111", 0x41, 0x11, 1, 1, 0,
        dec_load_or_throttle, /*is_priority=*/true,
    },
    {
        "Temp. liquido", data::Metric::CoolantTempC, "°C",
        "0105", 0x41, 0x05, 1, 1, 3000,
        dec_temp_c_minus_40,
    },
    {
        "Temp. aria aspirata", data::Metric::IntakeAirTempC, "°C",
        "010F", 0x41, 0x0F, 1, 1, 5000,
        dec_temp_c_minus_40,
    },
    {
        "Temp. esterna", data::Metric::AmbientTempC, "°C",
        "0146", 0x41, 0x46, 1, 1, 3000,
        dec_temp_c_minus_40,
    },
    {
        "Pressione MAP", data::Metric::BoostKpa, "kPa",
        "010B", 0x41, 0x0B, 1, 1, 0,
        dec_map_kpa, /*is_priority=*/true,
    },
    {
        "Tensione centralina", data::Metric::BatteryVolts, "V",
        "0142", 0x41, 0x42, 1, 2, 5000,
        dec_control_module_voltage,
    },
    {
        "EGT (se supportato)", data::Metric::EgtC, "°C",
        "0178", 0x41, 0x78, 1, 9, 2000,
        dec_egt_k,
    },
    {
        "Lambda banco 1", data::Metric::Afr, "AFR",
        "0124", 0x41, 0x24, 1, 4, 500,
        dec_lambda_afr,
    },
    /* --- Carburante (PID standard Mode 01) --------------------------------- */
    {
        "Livello carburante", data::Metric::FuelLevelPct, "%",
        "012F", 0x41, 0x2F, 1, 1, 2000,
        dec_fuel_level_pct,
    },
    {
        "Pressione carburante", data::Metric::FuelPressureKpa, "kPa",
        "0123", 0x41, 0x23, 1, 2, 1000,
        dec_fuel_pressure_kpa,
    },
    {
        "Pressione rail carburante", data::Metric::FuelPressureKpa, "kPa",
        "0159", 0x41, 0x59, 1, 2, 1000,
        dec_fuel_rail_pressure_kpa,
    },
    {
        "Portata carburante", data::Metric::FuelRateLh, "L/h",
        "015E", 0x41, 0x5E, 1, 2, 500,
        dec_fuel_rate_lh,
    },
    {
        "Temp. carburante", data::Metric::FuelTempC, "°C",
        "0126", 0x41, 0x26, 1, 1, 2000,
        dec_fuel_temp_c,
    },
    {
        "Anticipo iniezione", data::Metric::FuelInjectionTiming, "°",
        "015D", 0x41, 0x5D, 1, 2, 500,
        dec_injection_timing,
    },
    {
        "Etanolo %", data::Metric::EthanolPct, "%",
        "0152", 0x41, 0x52, 1, 1, 5000,
        dec_ethanol_pct,
    },
    {
        "Distanza da reset DTC", data::Metric::DistanceSinceClear, "km",
        "0131", 0x41, 0x31, 1, 2, 5000,
        dec_distance_since_clear,
    },
};
static_assert(sizeof(kStandardPids) / sizeof(kStandardPids[0]) == kStandardPidsCount,
              "aggiorna kStandardPidsCount in pids.h se aggiungi/rimuovi PID");

/*
 * PID Mode 22 (local identifier 16 bit).
 * I valori qui sotto sono quelli piu' comunemente funzionanti su Abarth 595
 * con motore 1.4 T-Jet MultiAir. Alcune versioni di centralina (MM10JF vs
 * MM10JE) possono usare identificatori diversi: in quel caso basta editare
 * `pid_id` qui sotto.
 */
const PidSpec kAbarthCustomPids[] = {
    {
        "Temp. olio motore", data::Metric::OilTempC, "°C",
        "22706E", 0x62, 0x706E, 2, 1, 1000,
        dec_fiat_oil_temp,
    },
    {
        "Pressione olio", data::Metric::OilPressureBar, "bar",
        "221955", 0x62, 0x1955, 2, 1, 500,
        dec_fiat_oil_pressure,
    },
    {
        "Boost reale", data::Metric::BoostRelBar, "bar",
        "221004", 0x62, 0x1004, 2, 2, 0,
        dec_fiat_boost_rel_bar, /*is_priority=*/true,
    },
    {
        "Temp. esterna (Mode 22)", data::Metric::AmbientTempC, "°C",
        "221002", 0x62, 0x1002, 2, 1, 3000,
        dec_temp_c_minus_40,
    },
};
static_assert(sizeof(kAbarthCustomPids) / sizeof(kAbarthCustomPids[0]) == kAbarthCustomPidsCount,
              "aggiorna kAbarthCustomPidsCount in pids.h se aggiungi/rimuovi PID");

/* === Parsing ============================================================= */

size_t parseHexPayload(std::string_view hex, uint8_t* out, size_t out_max,
                       uint8_t expected_sid) {
    // Rimuove spazi, tab e newline. Gestisce prefissi di header CAN come
    // "7E8 06 41 ..." (primo token e' l'header a 3-4 cifre, secondo e' la
    // lunghezza, poi i dati reali).
    uint8_t tmp[256];
    size_t  tmp_len = 0;
    int     high    = -1;
    for (char c : hex) {
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        int nib = hex_nibble(c);
        if (nib < 0) return 0;
        if (high < 0) { high = nib; continue; }
        if (tmp_len >= sizeof(tmp)) return 0;
        tmp[tmp_len++] = static_cast<uint8_t>((high << 4) | nib);
        high = -1;
    }
    if (high >= 0) return 0;

    // Il primo byte deve essere il SID di risposta atteso. Tolleriamo fino a
    // 3 byte iniziali (header CAN + lunghezza con ATH1/ATCAF0), ma NON cerchiamo
    // il SID in tutto il payload: 0x41/0x7F possono comparire come dati.
    size_t start = 0;
    if (expected_sid != 0) {
        bool found = false;
        for (size_t i = 0; i < tmp_len && i <= 3; ++i) {
            if (tmp[i] == expected_sid) { start = i; found = true; break; }
            if (i == 0 && tmp[0] == 0x7F) return 0;  // risposta negativa
        }
        if (!found) return 0;
    }
    if (start >= tmp_len) return 0;
    const size_t copy = std::min(out_max, tmp_len - start);
    std::memcpy(out, tmp + start, copy);
    return copy;
}

/* === Riassemblaggio risposte ELM327 multi-riga (ISO-TP) ================== */

namespace {
bool parse_hex_line(std::string_view line, std::vector<uint8_t>& out) {
    out.clear();
    int high = -1;
    for (char c : line) {
        if (c == ' ' || c == '\t') continue;
        const int nib = hex_nibble(c);
        if (nib < 0) return false;
        if (high < 0) { high = nib; continue; }
        out.push_back(static_cast<uint8_t>((high << 4) | nib));
        high = -1;
    }
    return high < 0 && !out.empty();
}

bool is_hex_text(std::string_view t) {
    if (t.empty()) return false;
    for (char c : t) if (hex_nibble(c) < 0) return false;
    return true;
}
}  // namespace

std::vector<std::vector<uint8_t>> splitElmMessages(std::string_view raw) {
    std::vector<std::vector<uint8_t>> msgs;
    std::vector<uint8_t> multi;       // messaggio ISO-TP in costruzione
    size_t multi_len = 0;             // lunghezza dichiarata (0 = sconosciuta)
    bool in_multi = false;
    int last_index = -1;
    std::vector<uint8_t> bytes;

    auto flush_multi = [&]() {
        if (in_multi && !multi.empty()) {
            if (multi_len > 0 && multi.size() > multi_len) multi.resize(multi_len);
            msgs.push_back(multi);
        }
        multi.clear();
        multi_len = 0;
        in_multi = false;
        last_index = -1;
    };

    size_t start = 0;
    for (size_t pos = 0; pos <= raw.size(); ++pos) {
        if (pos != raw.size() && raw[pos] != '\r' && raw[pos] != '\n') continue;
        std::string_view line = raw.substr(start, pos - start);
        start = pos + 1;
        // togli spazi
        std::string clean;
        for (char c : line) if (c != ' ' && c != '\t') clean.push_back(c);
        if (clean.empty()) continue;

        const size_t colon = clean.find(':');
        if (colon != std::string::npos && colon >= 1 && colon <= 2 &&
            is_hex_text(std::string_view(clean).substr(0, colon))) {
            // Riga di continuazione ISO-TP "N:"
            const int idx = static_cast<int>(std::strtol(clean.substr(0, colon).c_str(), nullptr, 16));
            if (!in_multi) {
                in_multi = true;  // messaggio senza riga di lunghezza
            } else if (idx == 0) {
                // "0:" dopo "F:" con lunghezza non ancora raggiunta = indice che riparte;
                // altrimenti e' un nuovo messaggio (es. seconda centralina)
                const bool wrap = last_index == 0xF && multi_len > 0 && multi.size() < multi_len;
                if (!wrap) { flush_multi(); in_multi = true; }
            }
            last_index = idx;
            if (parse_hex_line(std::string_view(clean).substr(colon + 1), bytes)) {
                multi.insert(multi.end(), bytes.begin(), bytes.end());
            }
            continue;
        }
        if (!is_hex_text(clean)) continue;  // SEARCHING..., ecc.

        if (clean.size() == 3) {
            // Riga con la lunghezza (sempre 3 cifre, es. "00B") del messaggio ISO-TP che segue
            flush_multi();
            in_multi = true;
            multi_len = std::strtoul(clean.c_str(), nullptr, 16);
            continue;
        }
        flush_multi();
        if (parse_hex_line(clean, bytes)) msgs.push_back(bytes);
    }
    flush_multi();
    return msgs;
}

bool extractPidData(const uint8_t* raw, size_t raw_len, const PidSpec& spec,
                    const uint8_t*& data_out) {
    if (raw_len < 1U + spec.pid_bytes + spec.data_bytes) return false;
    if (raw[0] != spec.response_mode) return false;
    if (spec.pid_bytes == 1) {
        if (raw[1] != (spec.pid_id & 0xFF)) return false;
    } else {
        if (raw[1] != ((spec.pid_id >> 8) & 0xFF)) return false;
        if (raw[2] != (spec.pid_id & 0xFF)) return false;
    }
    data_out = raw + 1 + spec.pid_bytes;
    return true;
}

/// Cerca un PidSpec nella tabella il cui response_mode+pid_id corrisponde ai
/// primi byte della risposta decodificata. Usato dal dispatch delle risposte
/// batch multi-PID. Ritorna nullptr se nessun match.
const PidSpec* findPidSpecByResponse(const uint8_t* raw, size_t raw_len,
                                     const PidSpec* table, size_t count) {
    if (raw_len < 1) return nullptr;
    for (size_t i = 0; i < count; ++i) {
        const PidSpec& s = table[i];
        if (raw[0] != s.response_mode) continue;
        if (s.pid_bytes == 1) {
            if (raw_len >= 2 && raw[1] == (s.pid_id & 0xFF)) return &s;
        } else {
            if (raw_len >= 3 &&
                raw[1] == ((s.pid_id >> 8) & 0xFF) &&
                raw[2] == (s.pid_id & 0xFF)) return &s;
        }
    }
    return nullptr;
}

}  // namespace abarth::obd
