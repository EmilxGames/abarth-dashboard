#include "config/settings.h"
#include "config/saved_links.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

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
constexpr const char* kKeyUiStyle     = "ui_style";
constexpr const char* kKeyUiV2Pending  = "ui_v2_pend";
constexpr const char* kKeyUiAccent     = "ui_accent";
constexpr const char* kKeyUiBackground = "ui_bg";
constexpr const char* kKeyBrMode       = "br_mode";
constexpr const char* kKeyBrDay        = "br_day";
constexpr const char* kKeyBrNight      = "br_night";
constexpr const char* kKeyBrDayFrom    = "br_dfrom";
constexpr const char* kKeyBrNightFrom  = "br_nfrom";
constexpr const char* kKeyLat          = "geo_lat";
constexpr const char* kKeyLon          = "geo_lon";
constexpr const char* kKeyAlOnScreen   = "al_screen";
constexpr const char* kKeyAlInSetup    = "al_setup";
constexpr const char* kKeyTank         = "tank_l";
constexpr const char* kKeyRefuelThr    = "rf_thr";
constexpr const char* kKeyRedline      = "redline";
constexpr const char* kKeyBootFail     = "boot_fail";
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
constexpr const char* kKeyOilWarmupV2   = "ow_v2";     ///< nuovi predefiniti gia' applicati
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
// Protegge i campi std::string di g_settings: current() ne fa una copia mentre
// altri task possono modificarli (revisione A6).
SemaphoreHandle_t g_mtx = xSemaphoreCreateRecursiveMutex();
struct Lock {
    Lock()  { if (g_mtx) xSemaphoreTakeRecursive(g_mtx, portMAX_DELAY); }
    ~Lock() { if (g_mtx) xSemaphoreGiveRecursive(g_mtx); }
};
nvs_handle_t g_handle = 0;

uint8_t clamp_brightness(uint8_t percent) {
    if (percent < 10)  return 10;  // stesso minimo dello slider
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

/// Soglia acqua per il riscaldamento olio: 50-100 °C
float clampWarmupWaterTemp(float t) {
    if (!(t >= 50.0f)) return 50.0f;
    return t > 100.0f ? 100.0f : t;
}
/// Durata riscaldamento olio: 1-15 minuti a passi di 30 s
uint16_t clampWarmupCountdown(uint32_t sec) {
    if (sec < 60) sec = 60;
    if (sec > 900) sec = 900;
    return static_cast<uint16_t>((sec + 15) / 30 * 30);
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
    {
        const uint8_t st = load_u8(kKeyUiStyle, 0);
        g_settings.ui_style = (st == 1) ? UiStyle::Racing : UiStyle::Classic;  // valori ignoti -> Classico
        const uint8_t ac = load_u8(kKeyUiAccent, 0);
        g_settings.ui_accent = ac <= 3 ? ac : 0;
        const uint8_t bg = load_u8(kKeyUiBackground, 0);
        g_settings.ui_background = bg <= 6 ? bg : 0;
        const uint8_t bm = load_u8(kKeyBrMode, 0);
        g_settings.bright_mode = bm <= 2 ? bm : 0;
        g_settings.bright_day = clamp_brightness(load_u8(kKeyBrDay, g_settings.bright_day));
        g_settings.bright_night = clamp_brightness(load_u8(kKeyBrNight, g_settings.bright_night));
        const uint32_t df = load_u32(kKeyBrDayFrom, g_settings.day_from_min);
        const uint32_t nf = load_u32(kKeyBrNightFrom, g_settings.night_from_min);
        g_settings.day_from_min = df < 1440 ? df : 7 * 60;
        g_settings.night_from_min = nf < 1440 ? nf : 19 * 60 + 30;
        const int32_t la = static_cast<int32_t>(load_u32(kKeyLat, static_cast<uint32_t>(static_cast<int32_t>(g_settings.lat_x100))));
        const int32_t lo = static_cast<int32_t>(load_u32(kKeyLon, static_cast<uint32_t>(static_cast<int32_t>(g_settings.lon_x100))));
        g_settings.lat_x100 = (la >= -9000 && la <= 9000) ? la : 4200;
        g_settings.lon_x100 = (lo >= -18000 && lo <= 18000) ? lo : 1250;
        g_settings.alerts_onscreen = load_u8(kKeyAlOnScreen, 1) != 0;
        g_settings.alerts_in_setup = load_u8(kKeyAlInSetup, 0) != 0;
        const uint8_t tl = load_u8(kKeyTank, 35);
        g_settings.tank_liters = (tl >= 15 && tl <= 120) ? tl : 35;
        const uint8_t rt = load_u8(kKeyRefuelThr, 8);
        g_settings.refuel_threshold_pct = (rt >= 4 && rt <= 30) ? rt : 8;
        const uint32_t rl = load_u32(kKeyRedline, 6200);
        g_settings.redline_rpm = (rl >= 3000 && rl <= 9000) ? static_cast<uint16_t>(rl) : 6200;
    }
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
    g_settings.oil_warmup_water_temp = clampWarmupWaterTemp(static_cast<float>(load_u8(kKeyOilWarmupWTemp, static_cast<uint8_t>(g_settings.oil_warmup_water_temp))));
    g_settings.oil_warmup_countdown_sec = clampWarmupCountdown(load_u32(kKeyOilWarmupTime, g_settings.oil_warmup_countdown_sec));
    // una sola volta: chi aveva ancora i vecchi valori predefiniti (80 C / 5 min)
    // passa ai nuovi (75 C / 3 min); un valore scelto diverso resta com'e'
    if (load_u8(kKeyOilWarmupV2, 0) == 0) {
        if (g_settings.oil_warmup_countdown_sec == 300) {
            g_settings.oil_warmup_countdown_sec = 180;
            save_u32(kKeyOilWarmupTime, 180);
        }
        if (g_settings.oil_warmup_water_temp == 80.0f) {
            g_settings.oil_warmup_water_temp = 75.0f;
            save_u8(kKeyOilWarmupWTemp, 75);
        }
        save_u8(kKeyOilWarmupV2, 1);
    }
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

Settings current() {
    Lock l;
    return g_settings;  // copia: nessun riferimento condiviso tra task
}

void previewBrightness(uint8_t percent) {
    display::setBrightness(static_cast<int>(clamp_brightness(percent)));
}

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

void setUiStyle(UiStyle style) {
    g_settings.ui_style = style;
    save_u8(kKeyUiStyle, static_cast<uint8_t>(style));
}

void setBrightnessMode(uint8_t mode) {
    g_settings.bright_mode = mode <= 2 ? mode : 0;
    save_u8(kKeyBrMode, g_settings.bright_mode);
}

void setBrightnessDayNight(uint8_t day_pct, uint8_t night_pct) {
    g_settings.bright_day = clamp_brightness(day_pct);
    g_settings.bright_night = clamp_brightness(night_pct);
    save_u8(kKeyBrDay, g_settings.bright_day);
    save_u8(kKeyBrNight, g_settings.bright_night);
}

void setDayNightTimes(uint16_t day_from_min, uint16_t night_from_min) {
    g_settings.day_from_min = day_from_min % 1440;
    g_settings.night_from_min = night_from_min % 1440;
    save_u32(kKeyBrDayFrom, g_settings.day_from_min);
    save_u32(kKeyBrNightFrom, g_settings.night_from_min);
}

void setLocation(int16_t lat_x100, int16_t lon_x100) {
    g_settings.lat_x100 = lat_x100 < -9000 ? -9000 : lat_x100 > 9000 ? 9000 : lat_x100;
    g_settings.lon_x100 = lon_x100 < -18000 ? -18000 : lon_x100 > 18000 ? 18000 : lon_x100;
    save_u32(kKeyLat, static_cast<uint32_t>(static_cast<int32_t>(g_settings.lat_x100)));
    save_u32(kKeyLon, static_cast<uint32_t>(static_cast<int32_t>(g_settings.lon_x100)));
}

void setAlertsOnScreen(bool on) {
    g_settings.alerts_onscreen = on;
    save_u8(kKeyAlOnScreen, on ? 1 : 0);
}

void setTankLiters(uint8_t l) {
    g_settings.tank_liters = l < 15 ? 15 : l > 120 ? 120 : l;
    save_u8(kKeyTank, g_settings.tank_liters);
}

void setRefuelThreshold(uint8_t pct) {
    g_settings.refuel_threshold_pct = pct < 4 ? 4 : pct > 30 ? 30 : pct;
    save_u8(kKeyRefuelThr, g_settings.refuel_threshold_pct);
}

void setAlertsInSetup(bool on) {
    g_settings.alerts_in_setup = on;
    save_u8(kKeyAlInSetup, on ? 1 : 0);
}

void setUiBackground(uint8_t idx) {
    g_settings.ui_background = idx <= 6 ? idx : 0;
    save_u8(kKeyUiBackground, g_settings.ui_background);
}

void setUiAccent(uint8_t idx) {
    g_settings.ui_accent = idx <= 3 ? idx : 0;
    save_u8(kKeyUiAccent, g_settings.ui_accent);
}

void setRedlineRpm(uint16_t rpm) {
    if (rpm < 3000 || rpm > 9000) return;
    g_settings.redline_rpm = rpm;
    save_u32(kKeyRedline, rpm);
}

uint8_t bootFailCount() { return load_u8(kKeyBootFail, 0); }
void setBootFailCount(uint8_t n) { save_u8(kKeyBootFail, n); }

bool isUiV2Pending() {
    return load_u8(kKeyUiV2Pending, 0) != 0;
}

void setUiV2Pending(bool pending) {
    save_u8(kKeyUiV2Pending, pending ? 1 : 0);
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
    { Lock l; g_settings.saved_dongle_mac = mac; }
    save_str(kKeyDongleMac, mac);
    // anche nell'elenco dei dongle salvati (svuotare non lo toglie dall'elenco)
    if (!mac.empty()) links::addBle("", mac);
}

void setSavedWifi(const std::string& ssid, const std::string& pass) {
    {
        Lock l;
        g_settings.saved_wifi_ssid = ssid;
        g_settings.saved_wifi_pass = pass;
    }
    save_str(kKeyWifiSsid, ssid);
    save_str(kKeyWifiPass, pass);
    // anche nell'elenco delle reti salvate, come rete del dongle OBD
    if (!ssid.empty()) links::addWifi(ssid, pass, links::WifiRole::Dongle);
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
    temp_c = clampWarmupWaterTemp(temp_c);
    g_settings.oil_warmup_water_temp = temp_c;
    save_u8(kKeyOilWarmupWTemp, static_cast<uint8_t>(temp_c));
}

void setOilWarmupCountdownSec(uint16_t sec) {
    sec = clampWarmupCountdown(sec);
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
