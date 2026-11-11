#include "ui_v2/settings_model.h"

#include <cstdio>
#include "config/connection.h"
#include "config/settings.h"
#include "storage/web_server.h"
#include "ui_v2/theme.h"

namespace abarth::ui_v2::setup {

namespace {
using namespace abarth::settings;
const RowInfo kRows[] = {
    {RowId::Brightness,   "DISPLAY",     "Luminosita'"},
    {RowId::Orientation,  "DISPLAY",     "Orientamento"},
    {RowId::Accent,       "DISPLAY",     "Colore accento"},
    {RowId::UiStyle,      "DISPLAY",     "Stile interfaccia"},
    {RowId::Redline,      "DISPLAY",     "Zona rossa giri"},
    {RowId::TempUnit,     "UNITA'",      "Temperatura"},
    {RowId::SpeedUnit,    "UNITA'",      "Velocita'"},
    {RowId::PressureUnit, "UNITA'",      "Pressione"},
    {RowId::Connection,   "COLLEGAMENTO","Modalita'"},
    {RowId::AutoConnect,  "COLLEGAMENTO","Connessione automatica"},
    {RowId::WebServer,    "STRUMENTI",   "Web Server (10 min)"},
    {RowId::DemoMode,     "STRUMENTI",   "Simulazione dati"},
    {RowId::Advanced,     "STRUMENTI",   "Impostazioni avanzate"},
};
}  // namespace

const RowInfo* rows(int& count) {
    count = sizeof(kRows) / sizeof(kRows[0]);
    return kRows;
}

void valueText(RowId id, char* out, int n) {
    const Settings s = current();
    switch (id) {
        case RowId::Brightness:   std::snprintf(out, n, "%u %%", s.brightness); break;
        case RowId::TempUnit:     std::snprintf(out, n, "%s", s.temp_unit == TempUnit::Fahrenheit ? "F" : "C"); break;
        case RowId::SpeedUnit:    std::snprintf(out, n, "%s", s.speed_unit == SpeedUnit::Mph ? "mph" : "km/h"); break;
        case RowId::PressureUnit: std::snprintf(out, n, "%s", s.pressure_unit == PressureUnit::Bar ? "bar"
                                                     : s.pressure_unit == PressureUnit::Kpa ? "kPa" : "bar + kPa"); break;
        case RowId::Orientation:  std::snprintf(out, n, "%s", s.screen_orientation == ScreenOrientation::Landscape ? "Orizzontale"
                                                     : s.screen_orientation == ScreenOrientation::Portrait ? "Verticale" : "Verticale capovolto"); break;
        case RowId::Connection:   std::snprintf(out, n, "%s", s.connection_mode == ConnectionMode::BleObd ? "OBD Bluetooth"
                                                     : s.connection_mode == ConnectionMode::WifiObd ? "OBD WiFi" : "CAN diretto"); break;
        case RowId::AutoConnect:  std::snprintf(out, n, "%s", s.auto_connect ? "Si" : "No"); break;
        case RowId::WebServer:    std::snprintf(out, n, "%s", web::isRunning() ? "Attivo" : "Spento"); break;
        case RowId::DemoMode:     std::snprintf(out, n, "%s", (s.demo_mode && s.debug_mode) ? "Attiva" : "Spenta"); break;
        case RowId::UiStyle:      std::snprintf(out, n, "%s", s.ui_style == UiStyle::Racing ? "Moderno" : "Classica"); break;
        case RowId::Accent:       std::snprintf(out, n, "%s", ui_v2::theme::kAccentNames[s.ui_accent <= 3 ? s.ui_accent : 0]); break;
        case RowId::Redline:      std::snprintf(out, n, "%u", s.redline_rpm); break;
        default:                  if (n > 0) out[0] = 0; break;
    }
}

const char* activate(RowId id, bool& needs_reboot) {
    needs_reboot = false;
    const Settings s = current();
    switch (id) {
        case RowId::TempUnit:
            setTempUnit(s.temp_unit == TempUnit::Celsius ? TempUnit::Fahrenheit : TempUnit::Celsius);
            return nullptr;
        case RowId::SpeedUnit:
            setSpeedUnit(s.speed_unit == SpeedUnit::KmH ? SpeedUnit::Mph : SpeedUnit::KmH);
            return nullptr;
        case RowId::PressureUnit:
            setPressureUnit(s.pressure_unit == PressureUnit::Bar ? PressureUnit::Kpa
                          : s.pressure_unit == PressureUnit::Kpa ? PressureUnit::Both : PressureUnit::Bar);
            return nullptr;
        case RowId::Orientation:
            setScreenOrientation(s.screen_orientation == ScreenOrientation::Landscape ? ScreenOrientation::Portrait
                               : s.screen_orientation == ScreenOrientation::Portrait ? ScreenOrientation::PortraitInverse
                               : ScreenOrientation::Landscape);
            needs_reboot = true;
            return "L'orientamento si applica al riavvio.";
        case RowId::Connection:
            connection::switchMode(s.connection_mode == ConnectionMode::BleObd ? ConnectionMode::WifiObd
                                 : s.connection_mode == ConnectionMode::WifiObd ? ConnectionMode::DirectCan
                                 : ConnectionMode::BleObd);
            return nullptr;
        case RowId::AutoConnect:
            setAutoConnect(!s.auto_connect);
            return nullptr;
        case RowId::WebServer:
            if (web::isRunning()) { web::stopServer(); return nullptr; }
            if (web::startServer() == ESP_ERR_INVALID_STATE) {
                return "Collega prima il display a una rete WiFi (Impostazioni avanzate > Connettivita').";
            }
            return nullptr;
        case RowId::DemoMode:
            if (!s.debug_mode) return "Attiva prima la Modalita' Debug in Impostazioni avanzate > Test & Simulazione.";
            setDemoMode(!s.demo_mode);
            return nullptr;
        case RowId::UiStyle:
            setUiStyle(s.ui_style == UiStyle::Racing ? UiStyle::Classic : UiStyle::Racing);
            needs_reboot = true;
            return "Lo stile si applica al riavvio.";
        case RowId::Accent:
            setUiAccent(static_cast<uint8_t>((s.ui_accent + 1) % 4));
            needs_reboot = true;
            return "Il colore accento si applica al riavvio.";
        case RowId::Redline: {
            // 5000 -> 7500 a passi di 250, poi si riparte
            uint16_t r = s.redline_rpm + 250;
            if (r > 7500) r = 5000;
            setRedlineRpm(r);
            return nullptr;
        }
        default:
            return nullptr;  // Brightness e Advanced gestiti dalla UI
    }
}

void brightnessPreview(int pct) { previewBrightness(static_cast<uint8_t>(pct)); }
void brightnessCommit(int pct)  { setBrightness(static_cast<uint8_t>(pct)); }
int  brightness()               { return current().brightness; }

}  // namespace abarth::ui_v2::setup
