#pragma once
/**
 * @file binding.h
 * @brief Collegamento dato -> widget: calcola testo e "livello" (normale,
 *        freddo, avviso, allarme, assente) e segnala se sono cambiati, cosi'
 *        il widget viene toccato solo quando serve. Indipendente dallo stile.
 */
#include <cmath>
#include <cstdio>
#include <cstring>
#include "ui_v2/data_fmt.h"
#include "ui_v2/metric_info.h"

namespace abarth::ui_v2 {

enum class Level : uint8_t { Missing, Normal, Low, Warn };

struct Binding {
    const MetricInfo* info = nullptr;
    char  text[16] = "";
    char  unit[8]  = "";
    Level level = Level::Missing;
    float value = 0.0f;      ///< valore grezzo (unita' interne), valido se level != Missing
    float shown = NAN;       ///< valore nella scala mostrata (per barre), quantizzato

    void bind(data::Metric m) { info = metricInfo(m); text[0] = 0; unit[0] = 0; level = Level::Missing; }

    /// Aggiorna; ritorna true se testo, unita' o livello sono cambiati.
    bool update(int64_t now_ms) {
        if (!info) return false;
        char t[16];
        const char* u = info->unit;
        Level lv = Level::Missing;
        float v = 0.0f;
        if (fmt::get(info->metric, now_ms, v)) {
            fmt::format(info->kind, v, info->decimals, info->unit, t, sizeof(t), u);
            lv = Level::Normal;
            if (!std::isnan(info->warn_high) && v >= info->warn_high) lv = Level::Warn;
            else if (!std::isnan(info->warn_low) && v <= info->warn_low) lv = Level::Low;
            value = v;
            const float p = std::pow(10.0f, info->decimals);
            shown = std::round(v * p) / p;
        } else {
            std::snprintf(t, sizeof(t), "%s", fmt::kNoData);
            if (info->kind == fmt::Kind::TempC)
                u = settings::current().temp_unit == settings::TempUnit::Fahrenheit ? fmt::kDegF : fmt::kDegC;
            else if (info->kind == fmt::Kind::SpeedKmh) u = settings::speedUnitSymbol();
            else if (info->kind == fmt::Kind::PressKpa) u = settings::pressureUnitSymbol();
            shown = NAN;
        }
        const bool changed = std::strcmp(t, text) != 0 || std::strcmp(u ? u : "", unit) != 0 || lv != level;
        if (changed) {
            std::snprintf(text, sizeof(text), "%s", t);
            std::snprintf(unit, sizeof(unit), "%s", u ? u : "");
            level = lv;
        }
        return changed;
    }

    /// Posizione 0..1 nella scala del catalogo (per barre); NaN se assente
    float fraction() const {
        if (level == Level::Missing || !info) return NAN;
        float f = (value - info->min_v) / (info->max_v - info->min_v);
        return f < 0 ? 0 : (f > 1 ? 1 : f);
    }
};

/// Minimo e massimo di sessione (per i widget "picco"), azzerabili.
struct PeakTracker {
    float min_v = NAN, max_v = NAN;
    void feed(const Binding& b) {
        if (b.level == Level::Missing) return;
        if (std::isnan(min_v) || b.value < min_v) min_v = b.value;
        if (std::isnan(max_v) || b.value > max_v) max_v = b.value;
    }
    void reset() { min_v = max_v = NAN; }
};

}  // namespace abarth::ui_v2
