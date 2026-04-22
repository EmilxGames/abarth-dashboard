#include "obd/pids.h"

#include <cctype>
#include <cstring>

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

bool dec_signed_temp_c(const uint8_t* d, size_t n, float& out) {
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

}  // namespace

/* === Tabelle PID ========================================================= */

const PidSpec kStandardPids[] = {
    {
        "Giri motore", data::Metric::Rpm, "RPM",
        "010C", 0x41, 0x0C, 1, 2, 100,
        dec_rpm,
    },
    {
        "Velocita'", data::Metric::Speed, "km/h",
        "010D", 0x41, 0x0D, 1, 1, 200,
        dec_speed,
    },
    {
        "Carico motore", data::Metric::EngineLoad, "%",
        "0104", 0x41, 0x04, 1, 1, 250,
        dec_load_or_throttle,
    },
    {
        "Acceleratore", data::Metric::Throttle, "%",
        "0111", 0x41, 0x11, 1, 1, 250,
        dec_load_or_throttle,
    },
    {
        "Temp. liquido", data::Metric::CoolantTempC, "°C",
        "0105", 0x41, 0x05, 1, 1, 1000,
        dec_temp_c_minus_40,
    },
    {
        "Temp. aria aspirata", data::Metric::IntakeAirTempC, "°C",
        "010F", 0x41, 0x0F, 1, 1, 1000,
        dec_temp_c_minus_40,
    },
    {
        "Temp. esterna", data::Metric::AmbientTempC, "°C",
        "0146", 0x41, 0x46, 1, 1, 5000,
        dec_temp_c_minus_40,
    },
    {
        "Pressione MAP", data::Metric::BoostKpa, "kPa",
        "010B", 0x41, 0x0B, 1, 1, 200,
        dec_map_kpa,
    },
    {
        "Tensione centralina", data::Metric::BatteryVolts, "V",
        "0142", 0x41, 0x42, 1, 2, 2000,
        dec_control_module_voltage,
    },
    {
        "EGT (se supportato)", data::Metric::EgtC, "°C",
        "0178", 0x41, 0x78, 1, 9, 500,
        dec_egt_k,
    },
    {
        "Lambda banco 1", data::Metric::Afr, "AFR",
        "0124", 0x41, 0x24, 1, 4, 500,
        dec_lambda_afr,
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
        "221004", 0x62, 0x1004, 2, 2, 200,
        dec_fiat_boost_rel_bar,
    },
};
static_assert(sizeof(kAbarthCustomPids) / sizeof(kAbarthCustomPids[0]) == kAbarthCustomPidsCount,
              "aggiorna kAbarthCustomPidsCount in pids.h se aggiungi/rimuovi PID");

/* === Parsing ============================================================= */

size_t parseHexPayload(std::string_view hex, uint8_t* out, size_t out_max) {
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

    // Se i primi byte sembrano un header (es. 0x7E, 0xE8) seguiti da una
    // lunghezza, li saltiamo. Riconosco il pattern: primo byte >= 0x40 e
    // secondo byte <= 0x08 non e' affidabile, quindi cerco 0x41 o 0x62
    // (mode response) dall'inizio e taglio prima.
    size_t start = 0;
    for (size_t i = 0; i + 1 < tmp_len; ++i) {
        if (tmp[i] == 0x41 || tmp[i] == 0x62 || tmp[i] == 0x7F) {
            start = i;
            break;
        }
    }
    if (start >= tmp_len) return 0;
    const size_t copy = std::min(out_max, tmp_len - start);
    std::memcpy(out, tmp + start, copy);
    return copy;
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

}  // namespace abarth::obd
