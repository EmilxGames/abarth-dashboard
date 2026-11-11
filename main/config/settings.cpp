#include "config/settings.h"

#include <esp_log.h>
#include <nvs_flash.h>

#include "display/board.h"

namespace abarth::settings {

namespace {
constexpr const char* TAG = "settings";
constexpr const char* kNvsNamespace = "abarth";

// Chiavi NVS (uint8_t per enum, uint8_t per bool, stringhe per MAC/SSID).
constexpr const char* kKeyBrightness  = "bright";
constexpr const char* kKeyTempUnit    = "tunit";
constexpr const char* kKeySpeedUnit   = "sunit";
constexpr const char* kKeyPressureUnit = "punit";
constexpr const char* kKeyUiTheme     = "uitheme";
constexpr const char* kKeyBgTexture   = "bgtexture";
constexpr const char* kKeyScreenOrient = "scrno";
constexpr const char* kKeyDarkTheme   = "darkthm";
constexpr const char* kKeyAutoConnect = "autoconn";
constexpr const char* kKeyDemoMode    = "demomode";
constexpr const char* kKeyDongleMac   = "dmac";
constexpr const char* kKeyWifiSsid    = "wssid";
constexpr const char* kKeyWifiPass    = "wpass";
constexpr const char* kKeyDisMetrics  = "dismetr";
constexpr const char* kKeyDebugMode   = "dbgmode";
constexpr const char* kKeyOilWarmupEn   = "ow_en";
constexpr const char* kKeyOilWarmupWTemp= "ow_wtemp";
constexpr const char* kKeyOilWarmupTime = "ow_time";
constexpr const char* kKeyOilColdRpm    = "ow_rpm";
constexpr const char* kKeyOilVirtDisplay= "ow_vdisp";
constexpr const char* kKeyCustTabEn     = "ctab_en";
constexpr const char* kKeyCustMask      = "ctab_msk";
constexpr const char* kKeyConnMode      = "conn_mode";
constexpr const char* kKeyCan1En        = "can1_en";
constexpr const char* kKeyCan1Tx        = "can1_tx";
constexpr const char* kKeyCan1Rx        = "can1_rx";
constexpr const char* kKeyCan1Baud      = "can1_baud";
constexpr const char* kKeyCan2En        = "can2_en";
constexpr const char* kKeyCan2Tx        = "can2_tx";
constexpr const char* kKeyCan2Rx        = "can2_rx";
constexpr const char* kKeyCan2Baud      = "can2_baud";
// Chiave v2: il vecchio valore "can_l_only" (default false) viene ignorato, cosi'
// dopo l'aggiornamento il C-CAN riparte sempre in ascolto passivo.
constexpr const char* kKeyCanListenOnly = "can_lo_v2";

Settings g_settings;
bool     g_ready = false;
nvs_handle_t g_handle = 0;

uint8_t clamp_brightness(uint8_t percent) {
    if (percent < 20)  return 20;
    if (percent > 100) return 100;
    return percent;
}

uint8_t load_u8(const char* key, uint8_t def) {
    uint8_t v = def;
    if (g_handle) {
        nvs_get_u8(g_handle, key, &v);
    }
    return v;
}

void save_u8(const char* key, uint8_t v) {
    if (g_handle) {
        nvs_set_u8(g_handle, key, v);
        nvs_commit(g_handle);
    }
}

uint32_t load_u32(const char* key, uint32_t def) {
    uint32_t v = def;
    if (g_handle) {
        nvs_get_u32(g_handle, key, &v);
    }
    return v;
}

void save_u32(const char* key, uint32_t v) {
    if (g_handle) {
        nvs_set_u32(g_handle, key, v);
        nvs_commit(g_handle);
    }
}

std::string load_str(const char* key) {
    if (!g_handle) return "";
    char buf[64] = {0};
    size_t len = sizeof(buf);
    if (nvs_get_str(g_handle, key, buf, &len) == ESP_OK) {
        return std::string(buf);
    }
    return "";
}

void save_str(const char* key, const std::string& v) {
    if (g_handle) {
        nvs_set_str(g_handle, key, v.c_str());
        nvs_commit(g_handle);
    }
}
}  // namespace

bool init() {
    if (g_ready) return true;
    esp_err_t err = nvs_open(kNvsNamespace, NVS_READWRITE, &g_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open fallito (%s), uso default in RAM", esp_err_to_name(err));
        g_ready = true;  // procediamo comunque con i default
        return false;
    }
    g_settings.brightness   = clamp_brightness(load_u8(kKeyBrightness, g_settings.brightness));
    g_settings.temp_unit    = static_cast<TempUnit>(load_u8(kKeyTempUnit, static_cast<uint8_t>(g_settings.temp_unit)));
    g_settings.speed_unit   = static_cast<SpeedUnit>(load_u8(kKeySpeedUnit, static_cast<uint8_t>(g_settings.speed_unit)));
    g_settings.pressure_unit = static_cast<PressureUnit>(load_u8(kKeyPressureUnit, static_cast<uint8_t>(g_settings.pressure_unit)));
    g_settings.ui_theme     = UiTheme::MinimalFunctional;
    g_settings.bg_texture   = static_cast<BgTexture>(load_u8(kKeyBgTexture, static_cast<uint8_t>(g_settings.bg_texture)));
    g_settings.screen_orientation = static_cast<ScreenOrientation>(load_u8(kKeyScreenOrient, static_cast<uint8_t>(g_settings.screen_orientation)));
    g_settings.dark_theme   = load_u8(kKeyDarkTheme, g_settings.dark_theme ? 1 : 0) != 0;
    g_settings.auto_connect = load_u8(kKeyAutoConnect, g_settings.auto_connect ? 1 : 0) != 0;
    g_settings.demo_mode    = load_u8(kKeyDemoMode, g_settings.demo_mode ? 1 : 0) != 0;
    g_settings.debug_mode   = load_u8(kKeyDebugMode, g_settings.debug_mode ? 1 : 0) != 0;
    g_settings.saved_dongle_mac = load_str(kKeyDongleMac);
    g_settings.saved_wifi_ssid  = load_str(kKeyWifiSsid);
    g_settings.saved_wifi_pass  = load_str(kKeyWifiPass);
    g_settings.disabled_metrics_mask = load_u32(kKeyDisMetrics, 0);
    g_settings.oil_warmup_enabled = load_u8(kKeyOilWarmupEn, g_settings.oil_warmup_enabled ? 1 : 0) != 0;
    g_settings.oil_warmup_water_temp = static_cast<float>(load_u8(kKeyOilWarmupWTemp, static_cast<uint8_t>(g_settings.oil_warmup_water_temp)));
    g_settings.oil_warmup_countdown_sec = static_cast<uint16_t>(load_u32(kKeyOilWarmupTime, g_settings.oil_warmup_countdown_sec));
    g_settings.oil_cold_rpm_limit = static_cast<float>(load_u32(kKeyOilColdRpm, static_cast<uint32_t>(g_settings.oil_cold_rpm_limit)));
    g_settings.oil_virtual_temp_display = load_u8(kKeyOilVirtDisplay, g_settings.oil_virtual_temp_display ? 1 : 0) != 0;
    g_settings.custom_tab_enabled = load_u8(kKeyCustTabEn, g_settings.custom_tab_enabled ? 1 : 0) != 0;
    constexpr uint32_t kDefCustomMask = (1U << static_cast<uint8_t>(abarth::data::Metric::Speed)) |
                                        (1U << static_cast<uint8_t>(abarth::data::Metric::Rpm)) |
                                        (1U << static_cast<uint8_t>(abarth::data::Metric::BoostRelBar)) |
                                        (1U << static_cast<uint8_t>(abarth::data::Metric::OilTempC)) |
                                        (1U << static_cast<uint8_t>(abarth::data::Metric::CoolantTempC));
    g_settings.custom_tab_metrics_mask = load_u32(kKeyCustMask, kDefCustomMask);
    g_settings.connection_mode = static_cast<ConnectionMode>(load_u8(kKeyConnMode, static_cast<uint8_t>(g_settings.connection_mode)));
    g_settings.can1_enabled = load_u8(kKeyCan1En, g_settings.can1_enabled ? 1 : 0) != 0;
    g_settings.can1_tx_pin = load_u8(kKeyCan1Tx, g_settings.can1_tx_pin);
    g_settings.can1_rx_pin = load_u8(kKeyCan1Rx, g_settings.can1_rx_pin);
    g_settings.can1_baud_rate = load_u32(kKeyCan1Baud, g_settings.can1_baud_rate);
    g_settings.can2_enabled = load_u8(kKeyCan2En, g_settings.can2_enabled ? 1 : 0) != 0;
    g_settings.can2_tx_pin = load_u8(kKeyCan2Tx, g_settings.can2_tx_pin);
    g_settings.can2_rx_pin = load_u8(kKeyCan2Rx, g_settings.can2_rx_pin);
    g_settings.can2_baud_rate = load_u32(kKeyCan2Baud, g_settings.can2_baud_rate);
    g_settings.can_listen_only = load_u8(kKeyCanListenOnly, g_settings.can_listen_only ? 1 : 0) != 0;
    g_ready = true;
    ESP_LOGI(TAG, "loaded: brightness=%u tempUnit=%u speedUnit=%u uiTheme=%u bg=%u darkTheme=%d autoConn=%d demoMode=%d",
             g_settings.brightness,
             static_cast<unsigned>(g_settings.temp_unit),
             static_cast<unsigned>(g_settings.speed_unit),
             static_cast<unsigned>(g_settings.ui_theme),
             static_cast<unsigned>(g_settings.bg_texture),
             g_settings.dark_theme,
             g_settings.auto_connect,
             g_settings.demo_mode);
    return true;
}

const Settings& current() { return g_settings; }

void setBrightness(uint8_t percent) {
    percent = clamp_brightness(percent);
    g_settings.brightness = percent;
    save_u8(kKeyBrightness, percent);
    display::setBrightness(static_cast<int>(percent));
}

void setTempUnit(TempUnit u) {
    g_settings.temp_unit = u;
    save_u8(kKeyTempUnit, static_cast<uint8_t>(u));
}

void setSpeedUnit(SpeedUnit u) {
    g_settings.speed_unit = u;
    save_u8(kKeySpeedUnit, static_cast<uint8_t>(u));
}

void setPressureUnit(PressureUnit u) {
    g_settings.pressure_unit = u;
    save_u8(kKeyPressureUnit, static_cast<uint8_t>(u));
}

float pressureInCurrentUnit(float kpa) {
    // 1 bar = 100 kPa
    if (g_settings.pressure_unit == PressureUnit::Bar) {
        return kpa / 100.0f;
    }
    // Kpa o Both: mostra il valore in kPa
    return kpa;
}

const char* pressureUnitSymbol() {
    switch (g_settings.pressure_unit) {
        case PressureUnit::Bar:  return "bar";
        case PressureUnit::Kpa:  return "kPa";
        case PressureUnit::Both: return "kPa/bar";
    }
    return "kPa";
}

void setUiTheme(UiTheme /*t*/) {
    g_settings.ui_theme = UiTheme::MinimalFunctional;
    save_u8(kKeyUiTheme, 0);
}

void setBgTexture(BgTexture texture) {
    g_settings.bg_texture = texture;
    save_u8(kKeyBgTexture, static_cast<uint8_t>(texture));
}

void setScreenOrientation(ScreenOrientation o) {
    g_settings.screen_orientation = o;
    save_u8(kKeyScreenOrient, static_cast<uint8_t>(o));
}

uint16_t orientationToDegrees(ScreenOrientation o) {
    switch (o) {
        case ScreenOrientation::Portrait:        return 90;
        case ScreenOrientation::PortraitInverse: return 270;
        case ScreenOrientation::Landscape:
        default:                                  return 0;
    }
}

void setAutoConnect(bool enable) {
    g_settings.auto_connect = enable;
    save_u8(kKeyAutoConnect, enable ? 1 : 0);
}

void setDemoMode(bool enable) {
    g_settings.demo_mode = enable;
    save_u8(kKeyDemoMode, enable ? 1 : 0);
}

void setDebugMode(bool enable) {
    g_settings.debug_mode = enable;
    save_u8(kKeyDebugMode, enable ? 1 : 0);
    if (!enable && g_settings.demo_mode) {
        setDemoMode(false);
    }
}

void setSavedDongleMac(const std::string& mac) {
    g_settings.saved_dongle_mac = mac;
    save_str(kKeyDongleMac, mac);
}

void setSavedWifi(const std::string& ssid, const std::string& pass) {
    g_settings.saved_wifi_ssid = ssid;
    g_settings.saved_wifi_pass = pass;
    save_str(kKeyWifiSsid, ssid);
    save_str(kKeyWifiPass, pass);
}

float tempInCurrentUnit(float c) {
    if (g_settings.temp_unit == TempUnit::Fahrenheit) {
        return c * 9.0f / 5.0f + 32.0f;
    }
    return c;
}

const char* tempUnitSymbol() {
    return g_settings.temp_unit == TempUnit::Fahrenheit ? " F" : " C";
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

bool isMetricVisible(abarth::data::Metric m) {
    uint8_t idx = static_cast<uint8_t>(m);
    if (idx >= static_cast<uint8_t>(abarth::data::Metric::Count_)) return true;
    return (g_settings.disabled_metrics_mask & (1U << idx)) == 0;
}

void setMetricVisible(abarth::data::Metric m, bool visible) {
    uint8_t idx = static_cast<uint8_t>(m);
    if (idx >= static_cast<uint8_t>(abarth::data::Metric::Count_)) return;
    if (visible) {
        g_settings.disabled_metrics_mask &= ~(1U << idx);
    } else {
        g_settings.disabled_metrics_mask |= (1U << idx);
    }
    save_u32(kKeyDisMetrics, g_settings.disabled_metrics_mask);
}

void setOilWarmupEnabled(bool enable) {
    g_settings.oil_warmup_enabled = enable;
    save_u8(kKeyOilWarmupEn, enable ? 1 : 0);
}

void setOilWarmupWaterTemp(float temp_c) {
    g_settings.oil_warmup_water_temp = temp_c;
    save_u8(kKeyOilWarmupWTemp, static_cast<uint8_t>(temp_c));
}

void setOilWarmupCountdownSec(uint16_t sec) {
    g_settings.oil_warmup_countdown_sec = sec;
    save_u32(kKeyOilWarmupTime, sec);
}

void setOilColdRpmLimit(float rpm) {
    g_settings.oil_cold_rpm_limit = rpm;
    save_u32(kKeyOilColdRpm, static_cast<uint32_t>(rpm));
}

void setOilVirtualTempDisplay(bool enable) {
    g_settings.oil_virtual_temp_display = enable;
    save_u8(kKeyOilVirtDisplay, enable ? 1 : 0);
}

bool isCustomTabEnabled() {
    return g_settings.custom_tab_enabled;
}

void setCustomTabEnabled(bool enable) {
    g_settings.custom_tab_enabled = enable;
    save_u8(kKeyCustTabEn, enable ? 1 : 0);
}

bool isMetricInCustomTab(abarth::data::Metric m) {
    uint8_t idx = static_cast<uint8_t>(m);
    if (idx >= static_cast<uint8_t>(abarth::data::Metric::Count_)) return false;
    return (g_settings.custom_tab_metrics_mask & (1U << idx)) != 0;
}

void setMetricInCustomTab(abarth::data::Metric m, bool in_custom) {
    uint8_t idx = static_cast<uint8_t>(m);
    if (idx >= static_cast<uint8_t>(abarth::data::Metric::Count_)) return;
    if (in_custom) {
        g_settings.custom_tab_metrics_mask |= (1U << idx);
    } else {
        g_settings.custom_tab_metrics_mask &= ~(1U << idx);
    }
    save_u32(kKeyCustMask, g_settings.custom_tab_metrics_mask);
}

uint32_t getCustomTabMask() {
    return g_settings.custom_tab_metrics_mask;
}

void setCustomTabMask(uint32_t mask) {
    g_settings.custom_tab_metrics_mask = mask;
    save_u32(kKeyCustMask, mask);
}

void setConnectionMode(ConnectionMode mode) {
    g_settings.connection_mode = mode;
    save_u8(kKeyConnMode, static_cast<uint8_t>(mode));
}

ConnectionMode getConnectionMode() {
    return g_settings.connection_mode;
}

void setCan1Enabled(bool en) {
    g_settings.can1_enabled = en;
    save_u8(kKeyCan1En, en ? 1 : 0);
}

bool isCan1Enabled() {
    return g_settings.can1_enabled;
}

void setCan1Config(uint8_t tx, uint8_t rx, uint32_t baud) {
    g_settings.can1_tx_pin = tx;
    g_settings.can1_rx_pin = rx;
    g_settings.can1_baud_rate = baud;
    save_u8(kKeyCan1Tx, tx);
    save_u8(kKeyCan1Rx, rx);
    save_u32(kKeyCan1Baud, baud);
}

void setCan2Enabled(bool en) {
    g_settings.can2_enabled = en;
    save_u8(kKeyCan2En, en ? 1 : 0);
}

bool isCan2Enabled() {
    return g_settings.can2_enabled;
}

void setCan2Config(uint8_t tx, uint8_t rx, uint32_t baud) {
    g_settings.can2_tx_pin = tx;
    g_settings.can2_rx_pin = rx;
    g_settings.can2_baud_rate = baud;
    save_u8(kKeyCan2Tx, tx);
    save_u8(kKeyCan2Rx, rx);
    save_u32(kKeyCan2Baud, baud);
}

void setCanListenOnly(bool listen_only) {
    g_settings.can_listen_only = listen_only;
    save_u8(kKeyCanListenOnly, listen_only ? 1 : 0);
}

void setCanPins(uint8_t tx, uint8_t rx) {
    setCan1Config(tx, rx, g_settings.can1_baud_rate);
}

void setCanBaudRate(uint32_t baud) {
    setCan1Config(g_settings.can1_tx_pin, g_settings.can1_rx_pin, baud);
}

}  // namespace abarth::settings
