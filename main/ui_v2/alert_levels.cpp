#include "ui_v2/alert_levels.h"
#include "config/alert_manager.h"

namespace abarth::ui_v2 {
namespace {
using config::AlertType;
const config::AlertRule* rule(AlertType t) {
    const auto* r = config::alerts().getRule(t);
    return (r && r->enabled) ? r : nullptr;
}
Level above(float v, float limit, float margin) {
    if (v >= limit) return Level::Warn;
    if (v >= limit - margin) return Level::Low;
    return Level::Normal;
}
}  // namespace

bool hasAlertThreshold(data::Metric m) {
    using data::Metric;
    return m == Metric::CoolantTempC || m == Metric::OilTempC || m == Metric::BoostRelBar ||
           m == Metric::BatteryVolts;
}

Level alertLevel(data::Metric m, float v) {
    using data::Metric;
    switch (m) {
        case Metric::CoolantTempC:
            if (const auto* r = rule(AlertType::HighCoolantTemp)) return above(v, r->threshold1, 5.0f);
            return above(v, 105.0f, 5.0f);
        case Metric::OilTempC:
            if (const auto* r = rule(AlertType::HighOilTemp)) return above(v, r->threshold1, 10.0f);
            return above(v, 125.0f, 10.0f);
        case Metric::BoostRelBar:
            if (const auto* r = rule(AlertType::HighBoostPressure)) return above(v, r->threshold1, 0.2f);
            return above(v, 1.5f, 0.2f);
        case Metric::BatteryVolts: {
            float low = 12.0f;
            if (const auto* r = rule(AlertType::LowBatteryVoltage)) low = r->threshold1;
            if (v < low || v > 15.5f) return Level::Warn;
            if (v < low + 0.4f || v > 15.0f) return Level::Low;
            return Level::Normal;
        }
        default:
            return Level::Normal;
    }
}
}  // namespace abarth::ui_v2
