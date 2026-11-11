#pragma once
/**
 * @file data_fmt.h
 * @brief Lettura dei valori dal data store con freschezza e unita' globali.
 */
#include <cstdint>
#include <cmath>
#include <cstdio>
#include "config/settings.h"
#include "data/data_store.h"

namespace abarth::ui_v2::fmt {

constexpr uint32_t kFreshMs = 2000;  ///< oltre questa eta' il valore si mostra "--"

/// true se il valore e' fresco; out = valore grezzo (unita' interne)
inline bool get(data::Metric m, int64_t now_ms, float& out) {
    const data::Sample s = data::store().get(m);
    if (!s.fresh(kFreshMs, now_ms)) return false;
    out = s.value;
    return true;
}

/// Segno meno e trattino "dato assente": U+2212 e U+2013 con i font Manrope,
/// ASCII con i font provvisori (che non hanno quei caratteri).
#define ABARTH_V2_MANROPE 1  // font Manrope con ° − – disponibili
#ifdef ABARTH_V2_MANROPE
constexpr const char* kMinus  = "\xE2\x88\x92";
constexpr const char* kNoData = "\xE2\x80\x93";
constexpr const char* kDegC   = "\xC2\xB0" "C";
constexpr const char* kDegF   = "\xC2\xB0" "F";
#else
constexpr const char* kMinus  = "-";
constexpr const char* kNoData = "-";
constexpr const char* kDegC   = "C";
constexpr const char* kDegF   = "F";
#endif

/// Tipo di grandezza, per conversione unita'
enum class Kind : uint8_t { Plain, TempC, SpeedKmh, PressKpa, BoostBar };

/// Converte nell'unita' scelta e scrive testo + simbolo unita'.
inline void format(Kind k, float v, int decimals, const char* plain_unit,
                   char* out, size_t out_sz, const char*& unit) {
    switch (k) {
        case Kind::TempC:
            v = settings::tempInCurrentUnit(v);
            unit = settings::current().temp_unit == settings::TempUnit::Fahrenheit ? kDegF : kDegC;
            break;
        case Kind::SpeedKmh: v = settings::speedInCurrentUnit(v); unit = settings::speedUnitSymbol(); break;
        case Kind::PressKpa: v = settings::pressureInCurrentUnit(v); unit = settings::pressureUnitSymbol(); break;
        case Kind::BoostBar: unit = "bar"; break;
        default:             unit = plain_unit; break;
    }
    // Negativi con il segno meno tipografico; mai "-0"
    const float p = (decimals == 0) ? 1.0f : (decimals == 1 ? 10.0f : 100.0f);
    if (std::round(v * p) < 0) std::snprintf(out, out_sz, "%s%.*f", kMinus, decimals, -v);
    else std::snprintf(out, out_sz, "%.*f", decimals, std::fabs(v) < 0.5f / p ? 0.0f : v);
}

}  // namespace abarth::ui_v2::fmt
