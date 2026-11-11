#pragma once
/**
 * @file data_fmt.h
 * @brief Lettura dei valori dal data store con freschezza e unita' globali.
 */
#include <cstdint>
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

/// Tipo di grandezza, per conversione unita'
enum class Kind : uint8_t { Plain, TempC, SpeedKmh, PressKpa, BoostBar };

/// Converte nell'unita' scelta e scrive testo + simbolo unita'.
inline void format(Kind k, float v, int decimals, const char* plain_unit,
                   char* out, size_t out_sz, const char*& unit) {
    switch (k) {
        case Kind::TempC:    v = settings::tempInCurrentUnit(v);  unit = settings::tempUnitSymbol(); break;
        case Kind::SpeedKmh: v = settings::speedInCurrentUnit(v); unit = settings::speedUnitSymbol(); break;
        case Kind::PressKpa: v = settings::pressureInCurrentUnit(v); unit = settings::pressureUnitSymbol(); break;
        case Kind::BoostBar: unit = "bar"; break;
        default:             unit = plain_unit; break;
    }
    if (k == Kind::BoostBar) std::snprintf(out, out_sz, "%+.*f", decimals, v);
    else std::snprintf(out, out_sz, "%.*f", decimals, v);
}

}  // namespace abarth::ui_v2::fmt
