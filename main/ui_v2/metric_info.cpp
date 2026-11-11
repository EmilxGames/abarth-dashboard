#include "ui_v2/metric_info.h"
#include <cmath>

namespace abarth::ui_v2 {
namespace {
using data::Metric;
using fmt::Kind;
constexpr float N = NAN;

const MetricInfo kCatalog[] = {
    {Metric::Rpm,                 "GIRI",        "Giri motore",               Kind::Plain,    "rpm", 0, 0, 7000, N, 6200},
    {Metric::Speed,               "VELOCIT\xC3\x80",   "Velocit\xC3\xA0",                 Kind::SpeedKmh, "",    0, 0, 220, N, N},
    {Metric::BoostRelBar,         "TURBO",       "Pressione turbo (relativa)",Kind::BoostBar, "bar", 2, -0.5f, 1.8f, N, 1.5f},
    {Metric::BoostKpa,            "MAP",         "Pressione collettore (MAP)",Kind::PressKpa, "",    0, 20, 260, N, N},
    {Metric::Throttle,            "ACCELERATORE","Posizione acceleratore",    Kind::Plain,    "%",   0, 0, 100, N, N},
    {Metric::EngineLoad,          "CARICO",      "Carico motore",             Kind::Plain,    "%",   0, 0, 100, N, N},
    {Metric::CoolantTempC,        "ACQUA",       "Temperatura liquido",       Kind::TempC,    "",    0, 40, 130, 60, 105},
    {Metric::OilTempC,            "OLIO",        "Temperatura olio",          Kind::TempC,    "",    0, 50, 150, 70, 125},
    {Metric::IntakeAirTempC,      "ARIA ASP.",   "Temperatura aria aspirata", Kind::TempC,    "",    0, -10, 80, N, 60},
    {Metric::AmbientTempC,        "ESTERNA",     "Temperatura esterna",       Kind::TempC,    "",    0, -20, 50, N, N},
    {Metric::EgtC,                "EGT",         "Temperatura gas scarico",   Kind::TempC,    "",    0, 100, 950, N, 900},
    {Metric::OilPressureBar,      "PRESS. OLIO", "Pressione olio",            Kind::Plain,    "bar", 1, 0, 8, 1.0f, N},
    {Metric::BatteryVolts,        "BATTERIA",    "Tensione batteria",         Kind::Plain,    "V",   1, 10, 16, 12.0f, 15.0f},
    {Metric::Afr,                 "AFR",         "Rapporto aria/benzina",     Kind::Plain,    "",    1, 10, 20, N, N},
    {Metric::FuelLevelPct,        "SERBATOIO",   "Livello carburante",        Kind::Plain,    "%",   0, 0, 100, 12, N},
    {Metric::FuelPressureKpa,     "RAIL",        "Pressione rail carburante", Kind::PressKpa, "",    0, 0, 16000, N, N},
    {Metric::FuelRateLh,          "PORTATA",     "Portata carburante",        Kind::Plain,    "L/h", 1, 0, 30, N, N},
    {Metric::FuelConsumptionL100, "CONSUMO",     "Consumo istantaneo",        Kind::Plain,    "L/100",1, 0, 30, N, N},
    {Metric::FuelTempC,           "TEMP. BENZ.", "Temperatura carburante",    Kind::TempC,    "",    0, -10, 80, N, N},
    {Metric::FuelInjectionTiming, "ANTICIPO",    "Anticipo iniezione",        Kind::Plain,    "deg", 1, -20, 40, N, N},
    {Metric::EthanolPct,          "ETANOLO",     "Percentuale etanolo",       Kind::Plain,    "%",   0, 0, 100, N, N},
    {Metric::DistanceSinceClear,  "KM DA RESET", "Km dall'ultimo reset DTC",  Kind::Plain,    "km",  0, 0, 10000, N, N},
};
}  // namespace

const MetricInfo* metricCatalog(size_t& count) {
    count = sizeof(kCatalog) / sizeof(kCatalog[0]);
    return kCatalog;
}

const MetricInfo* metricInfo(data::Metric m) {
    for (const auto& i : kCatalog) if (i.metric == m) return &i;
    return nullptr;
}

}  // namespace abarth::ui_v2
