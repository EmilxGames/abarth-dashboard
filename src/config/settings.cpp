#include "config/settings.h"

#include <Preferences.h>
#include <esp_log.h>

#include "display/board.h"

namespace abarth::settings {

namespace {
constexpr const char* TAG = "settings";
constexpr const char* kNvsNamespace = "abarth";

constexpr const char* kKeyBrightness = "bright";
constexpr const char* kKeyTempUnit   = "tunit";
constexpr const char* kKeySpeedUnit  = "sunit";
constexpr const char* kKeyDarkTheme  = "dark";

Settings     g_settings;
Preferences  g_prefs;
bool         g_ready = false;

uint8_t clamp_brightness(uint8_t percent) {
    if (percent < 10)  return 10;
    if (percent > 100) return 100;
    return percent;
}
}  // namespace

bool init() {
    if (g_ready) return true;
    if (!g_prefs.begin(kNvsNamespace, /*readOnly=*/false)) {
        ESP_LOGE(TAG, "Preferences::begin fallito, uso default in RAM");
        g_ready = true;  // procediamo comunque con i default
        return false;
    }
    g_settings.brightness = clamp_brightness(
        g_prefs.getUChar(kKeyBrightness, g_settings.brightness));
    g_settings.temp_unit = static_cast<TempUnit>(
        g_prefs.getUChar(kKeyTempUnit, static_cast<uint8_t>(g_settings.temp_unit)));
    g_settings.speed_unit = static_cast<SpeedUnit>(
        g_prefs.getUChar(kKeySpeedUnit, static_cast<uint8_t>(g_settings.speed_unit)));
    g_settings.dark_theme = g_prefs.getBool(kKeyDarkTheme, g_settings.dark_theme);
    g_ready = true;
    ESP_LOGI(TAG,
             "loaded: brightness=%u tempUnit=%u speedUnit=%u darkTheme=%d",
             g_settings.brightness,
             static_cast<unsigned>(g_settings.temp_unit),
             static_cast<unsigned>(g_settings.speed_unit),
             g_settings.dark_theme);
    return true;
}

const Settings& current() { return g_settings; }

void setBrightness(uint8_t percent) {
    percent = clamp_brightness(percent);
    g_settings.brightness = percent;
    if (g_ready) {
        g_prefs.putUChar(kKeyBrightness, percent);
    }
    display::setBrightness(static_cast<int>(percent));
}

void setTempUnit(TempUnit u) {
    g_settings.temp_unit = u;
    if (g_ready) {
        g_prefs.putUChar(kKeyTempUnit, static_cast<uint8_t>(u));
    }
}

void setSpeedUnit(SpeedUnit u) {
    g_settings.speed_unit = u;
    if (g_ready) {
        g_prefs.putUChar(kKeySpeedUnit, static_cast<uint8_t>(u));
    }
}

float tempInCurrentUnit(float c) {
    if (g_settings.temp_unit == TempUnit::Fahrenheit) {
        return c * 9.0f / 5.0f + 32.0f;
    }
    return c;
}

const char* tempUnitSymbol() {
    return g_settings.temp_unit == TempUnit::Fahrenheit ? "\u00b0F" : "\u00b0C";
}

float speedInCurrentUnit(float kmh) {
    if (g_settings.speed_unit == SpeedUnit::Mph) {
        return kmh * 0.621371f;
    }
    return kmh;
}

const char* speedUnitSymbol() {
    return g_settings.speed_unit == SpeedUnit::Mph ? "mph" : "km/h";
}

}  // namespace abarth::settings
