#include "ui/ui.h"

#include <lvgl.h>
#include <esp_log.h>

#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "config/alert_manager.h"
#include "config/app_config.h"
#include "config/settings.h"
#include "config/warmup_manager.h"
#include "data/data_store.h"
#include "display/lvgl_v8_port.h"
#include "obd/obd_manager.h"
#include "can/can_manager.h"
#include <esp_heap_caps.h>
#include "storage/sd_card_manager.h"
#include "ui/settings_tab.h"
#include "ui/scroll_bench.h"
#include "ui/abarth_logo_png.h"
#include "ui/oil_blue_png.h"

namespace abarth::ui {

namespace {
constexpr const char* TAG = "ui";

using abarth::data::Metric;
using abarth::data::Sample;

// --- Helper per disegno vettoriale sui canvas degli sfondi procedurali ---
// Disegna un "punto spesso" (per segmenti/curve) su un buffer RGB565.
inline void drawSeg_helper(uint16_t* cbuf, int W, int H,
                            float px, float py, float w, uint16_t color) {
    int ir = (int)(w + 1);
    int icx = (int)(px);
    int icy = (int)(py);
    for (int dy = -ir; dy <= ir; ++dy) {
        for (int dx = -ir; dx <= ir; ++dx) {
            if (dx * dx + dy * dy <= (int)(w * w)) {
                int fx = icx + dx;
                int fy = icy + dy;
                if (fx >= 0 && fx < W && fy >= 0 && fy < H) {
                    cbuf[fy * W + fx] = color;
                }
            }
        }
    }
}
// Variante: disegna direttamente un segmento tra due punti.
inline void drawSeg_helper2(uint16_t* cbuf, int W, int H,
                             float x0, float y0, float x1, float y1,
                             float w, uint16_t color) {
    float dx = x1 - x0, dy = y1 - y0;
    int steps = (int)(sqrtf(dx * dx + dy * dy) + 1);
    for (int s = 0; s <= steps; ++s) {
        float t = (float)s / steps;
        drawSeg_helper(cbuf, W, H, x0 + dx * t, y0 + dy * t, w, color);
    }
}

/// Categoria del valore per decidere l'unita' visualizzata.
enum class ValueKind : uint8_t {
    Generic = 0,   ///< usa `unit_str` direttamente
    TempC,         ///< converti in base a settings::temp_unit
    SpeedKmh,      ///< converti in base a settings::speed_unit
    PressureKpa,   ///< converti in base a settings::pressure_unit (kPa/bar/both)
};



lv_obj_t* g_conn_badge         = nullptr;
lv_obj_t* g_conn_icon          = nullptr;
lv_obj_t* g_conn_label         = nullptr;
lv_obj_t* g_state_label        = nullptr;
lv_obj_t* g_time_label         = nullptr;
lv_obj_t* g_counters_label     = nullptr;
lv_obj_t* g_sd_label           = nullptr;

lv_obj_t* g_warmup_badge       = nullptr;
lv_obj_t* g_warmup_icon        = nullptr;
lv_obj_t* g_warmup_lbl         = nullptr;

lv_obj_t* g_status_state_label = nullptr;
lv_obj_t* g_device_label       = nullptr;
lv_obj_t* g_ok_counter_label   = nullptr;
lv_obj_t* g_err_counter_label  = nullptr;
lv_obj_t* g_last_err_label     = nullptr;
lv_obj_t* g_main_tabview        = nullptr;

/* --- Minimal Funzionale: cockpit essenziale e strisce telemetriche ------ */

namespace mf {
    constexpr uint32_t BG_DEEP        = 0x060709;  // Nero OLED profondissimo
    constexpr uint32_t SURFACE_DARK   = 0x0E1015;  // Superficie card/strip
    constexpr uint32_t SURFACE_BORDER = 0x1A1E26;  // Bordi ultra-sottili
    constexpr uint32_t TEXT_WHITE     = 0xF8FAFC;  // Bianco nitido primario
    constexpr uint32_t TEXT_MUTED     = 0x7E8696;  // Grigio ardesia leggibile
    constexpr uint32_t TEXT_DIM       = 0x475569;  // Grigio sobrio per label
    constexpr uint32_t ACCENT_RED     = 0xEF4444;  // Rosso Abarth racing
    constexpr uint32_t ACCENT_AMBER   = 0xF59E0B;  // Ambra per riscaldamento/warning
    constexpr uint32_t ACCENT_CYAN    = 0x06B6D4;  // Ciano per temperature fredde
    constexpr uint32_t ACCENT_GREEN   = 0x10B981;  // Verde brillante per marce/OK
    constexpr uint32_t BAR_TRACK      = 0x161820;  // Traccia barra di fondo
}

struct MinimalDashboard {
    bool active = false;

    // Cockpit Containers & Cards for dynamic visibility reflow
    lv_obj_t* rev_card         = nullptr;
    lv_obj_t* mid_row          = nullptr;
    lv_obj_t* spd_card         = nullptr;
    lv_obj_t* dyn_card         = nullptr;
    lv_obj_t* r_ld             = nullptr;
    lv_obj_t* r_th             = nullptr;
    lv_obj_t* btm_row          = nullptr;
    lv_obj_t* p1               = nullptr;
    lv_obj_t* p2               = nullptr;
    lv_obj_t* p3               = nullptr;
    lv_obj_t* p4               = nullptr;

    // Cockpit / Motore Tab
    lv_obj_t* rev_bar          = nullptr;
    lv_obj_t* rev_label        = nullptr;
    lv_obj_t* shift_light      = nullptr;
    lv_obj_t* speed_label      = nullptr;
    lv_obj_t* speed_unit_label = nullptr;
    lv_obj_t* gear_label       = nullptr;
    lv_obj_t* load_bar         = nullptr;
    lv_obj_t* load_label       = nullptr;
    lv_obj_t* throttle_bar     = nullptr;
    lv_obj_t* throttle_label   = nullptr;

    // 4 Bottom Telemetry Pillars in Cockpit
    lv_obj_t* p1_boost_val     = nullptr;
    lv_obj_t* p1_boost_bar     = nullptr;
    lv_obj_t* p2_coolant_val   = nullptr;
    lv_obj_t* p2_coolant_badge = nullptr;
    lv_obj_t* p3_oil_val       = nullptr;
    lv_obj_t* p3_oil_badge     = nullptr;
    lv_obj_t* p4_batt_val      = nullptr;
    lv_obj_t* p4_afr_val       = nullptr;
};

MinimalDashboard g_mf_dash;

struct MinimalStrip {
    Metric      metric;
    ValueKind   kind;
    int         decimals;
    const char* unit_str;
    float       min_val;
    float       max_val;
    lv_color_t  color;
    lv_obj_t*   row;
    lv_obj_t*   val_label;
    lv_obj_t*   unit_label;
    lv_obj_t*   bar;
    char        prev_text[32];
    int         last_bar_val;
    bool        muted;
};

std::array<MinimalStrip, 24> g_mf_strips;
size_t g_mf_strip_count = 0;

std::array<MinimalStrip, 24> g_custom_strips;
size_t g_custom_strip_count = 0;
lv_obj_t* g_t_custom = nullptr;
lv_obj_t* g_tab_btn_custom = nullptr;

void update_minimal_dashboard(int64_t now_ms);

/* --- stili ---------------------------------------------------------------- */

lv_style_t style_card;
lv_style_t style_title;
lv_style_t style_value;
lv_style_t style_value_muted;   ///< testo piccolo/grigio per stati "Disconnesso", ecc.
lv_style_t style_unit;
bool       styles_ready = false;
lv_color_t g_accent_color = LV_COLOR_MAKE(0xCC, 0x30, 0x30);  ///< accent corrente del tema

void ensure_styles() {
    if (styles_ready) return;
    styles_ready = true;

    lv_color_t card_bg     = lv_color_hex(mf::SURFACE_DARK);
    lv_color_t card_border = lv_color_hex(mf::SURFACE_BORDER);
    lv_color_t title_col   = lv_color_hex(mf::TEXT_MUTED);
    lv_color_t unit_col    = lv_color_hex(mf::ACCENT_RED);
    lv_color_t accent_col  = lv_color_hex(mf::ACCENT_RED);
    lv_color_t muted_col   = lv_color_hex(mf::TEXT_MUTED);
    int radius = 10, border_w = 1, pad = 12, pad_row = 8;

    lv_style_init(&style_card);
    lv_style_set_radius(&style_card, radius);
    lv_style_set_bg_opa(&style_card, LV_OPA_COVER);
    lv_style_set_bg_color(&style_card, card_bg);
    lv_style_set_border_width(&style_card, border_w);
    lv_style_set_border_color(&style_card, card_border);
    lv_style_set_pad_all(&style_card, pad);
    lv_style_set_pad_row(&style_card, pad_row);

    lv_style_init(&style_title);
    lv_style_set_text_color(&style_title, title_col);
    lv_style_set_text_font(&style_title, &lv_font_montserrat_14);

    lv_style_init(&style_value);
    lv_style_set_text_color(&style_value, lv_color_hex(mf::TEXT_WHITE));
    lv_style_set_text_font(&style_value, &lv_font_montserrat_48);

    lv_style_init(&style_value_muted);
    lv_style_set_text_color(&style_value_muted, muted_col);
    lv_style_set_text_font(&style_value_muted, &lv_font_montserrat_20);

    lv_style_init(&style_unit);
    lv_style_set_text_color(&style_unit, unit_col);
    lv_style_set_text_font(&style_unit, &lv_font_montserrat_16);

    g_accent_color = accent_col;
}



/// Stato generale della connessione OBD → testo da mostrare sui valori in
/// assenza di dati validi. Ritorna nullptr se il dongle e' online (e quindi
/// l'assenza di dati puo' dipendere dal singolo PID, non dalla connessione).
const char* connection_placeholder() {
    if (abarth::settings::current().demo_mode) {
        return nullptr; // In Simulazione mostriamo sempre i dati telemetrici sintetici
    }
    using abarth::obd::ObdState;
    switch (abarth::obd::obd().state()) {
        case ObdState::Disconnected: return "Disconnesso";
        case ObdState::Scanning:     return "Ricerca dongle...";
        case ObdState::Connecting:   return "Connessione...";
        case ObdState::Initializing: return "Init ELM...";
        case ObdState::Error:        return "Errore";
        case ObdState::Polling:      return nullptr;  // il PID e' responsabile
    }
    return nullptr;
}

/// Riempie `out` con la rappresentazione testuale del valore e restituisce
/// true se il valore e' "reale" (numerico, fresco), false se e' un placeholder
/// (stato connessione, non supportato, ecc.).
bool format_value(ValueKind kind, int decimals, const Sample& s_in, char* out, size_t out_sz, int64_t now_ms) {
    Sample s = s_in;

    // 1) Nessuna connessione -> mostra lo stato generale.
    if (const char* p = connection_placeholder()) {
        std::snprintf(out, out_sz, "%s", p);
        return false;
    }
    // 2) Connesso ma la ECU ha risposto NO DATA a questo PID.
    if (!s.supported) {
        std::snprintf(out, out_sz, "N/D");
        return false;
    }
    // 3) Connesso, PID supportato, ma NON abbiamo ancora MAI letto un valore.
    if (!s.valid() || std::isnan(s.value)) {
        std::snprintf(out, out_sz, "Attesa...");
        return false;
    }
    // 4) Abbiamo gia' un valore valido: lo manteniamo VISIBILE fino a 120s
    if (!s.fresh(120000, now_ms)) {
        std::snprintf(out, out_sz, "Attesa...");
        return false;
    }
    // 5) Valore OK: conversione unita' se richiesto.
    float v = s.value;
    if (kind == ValueKind::TempC)       v = settings::tempInCurrentUnit(v);
    if (kind == ValueKind::SpeedKmh)    v = settings::speedInCurrentUnit(v);
    if (kind == ValueKind::PressureKpa) v = settings::pressureInCurrentUnit(v);
    if (decimals <= 0) std::snprintf(out, out_sz, "%.0f", v);
    else               std::snprintf(out, out_sz, "%.*f", decimals, v);
    return true;
}

void update_header_clock() {
    if (!g_time_label) return;
    static int  last_hh_mm = -2;   // -1 = "vuoto" gia' impostato; -2 = mai
    time_t now = time(nullptr);
    struct tm ti;
    localtime_r(&now, &ti);
    if (ti.tm_year + 1900 < 2024) {
        if (last_hh_mm != -1) {
            lv_label_set_text(g_time_label, "");
            last_hh_mm = -1;
        }
        return;
    }
    const int hh_mm = ti.tm_hour * 60 + ti.tm_min;
    if (hh_mm == last_hh_mm) return;   // stesso minuto: nessun write LVGL
    last_hh_mm = hh_mm;
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%02d:%02d", ti.tm_hour, ti.tm_min);
    lv_label_set_text(g_time_label, buf);
}

void on_tick(lv_timer_t* /*t*/) {
    const int64_t now_ms = esp_timer_get_time() / 1000;

    // Se la Modalita' Simulazione e' attiva, genera dati telemetrici sintetici
    if (abarth::settings::current().demo_mode) {
        static float sim_rpm   = 900.0f;
        static float sim_speed = 0.0f;
        static float sim_boost = -0.20f;
        static bool  sim_accel = true;

        if (sim_accel) {
            sim_rpm   += 80.0f;
            sim_speed += 1.2f;
            sim_boost += 0.03f;
            if (sim_rpm >= 6300.0f) {
                sim_rpm   = 3400.0f; // Cambio marcia!
                sim_boost = 0.35f;
                if (sim_speed >= 170.0f) {
                    sim_accel = false;
                }
            }
        } else {
            sim_rpm   -= 90.0f;
            sim_speed -= 1.5f;
            sim_boost -= 0.04f;
            if (sim_speed <= 10.0f) {
                sim_rpm   = 900.0f;
                sim_speed = 0.0f;
                sim_boost = -0.20f;
                sim_accel = true;
            }
        }

        auto& ds = abarth::data::store();
        ds.set(Metric::Rpm, sim_rpm);
        ds.set(Metric::Speed, sim_speed);
        ds.set(Metric::EngineLoad, (sim_rpm / 7000.0f) * 100.0f);
        ds.set(Metric::Throttle, (sim_rpm / 7000.0f) * 95.0f);
        ds.set(Metric::BoostRelBar, sim_boost);
        ds.set(Metric::CoolantTempC, 88.0f);
        ds.set(Metric::OilTempC, 95.0f);
        ds.set(Metric::IntakeAirTempC, 28.0f);
        ds.set(Metric::AmbientTempC, 22.0f);
        ds.set(Metric::EgtC, 640.0f);
        ds.set(Metric::BoostKpa, 145.0f);
        ds.set(Metric::BatteryVolts, 13.8f);
        ds.set(Metric::Afr, 14.7f);
        // --- Demo: carburante ---
        ds.set(Metric::FuelLevelPct, 62.0f);
        ds.set(Metric::FuelRateLh, (sim_rpm / 7000.0f) * 7.5f);     // 0..7.5 L/h
        ds.set(Metric::FuelPressureKpa, 380.0f);
        ds.set(Metric::FuelConsumptionL100,
               sim_speed > 5.0f ? ((sim_rpm / 7000.0f) * 7.5f) / sim_speed * 100.0f : 0.0f);
        ds.set(Metric::FuelTempC, 42.0f);
        ds.set(Metric::FuelInjectionTiming, 12.5f);
        ds.set(Metric::EthanolPct, 0.0f);   // E0 in Italia di default
        ds.set(Metric::DistanceSinceClear, 1250.0f + sim_speed);
    }

    // Valutazione regole allarmi e avvisi telematici
    abarth::config::alerts().checkAlerts();

    // Aggiornamento logica di warmup & stima temperatura olio
    abarth::warmup::warmup_mgr().update();

    // Aggiornamento Badge Countdown Warmup in Header (visibile su tutte le schede)
    if (g_warmup_badge && g_warmup_lbl) {
        using namespace abarth::warmup;
        const WarmupState wst = warmup_mgr().state();
        if (wst == WarmupState::Disabled) {
            lv_obj_add_flag(g_warmup_badge, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_clear_flag(g_warmup_badge, LV_OBJ_FLAG_HIDDEN);
            static WarmupState last_wst = static_cast<WarmupState>(255);
            static uint16_t last_rem_sec = 9999;
            const uint16_t rem_sec = warmup_mgr().remainingSeconds();

            if (wst != last_wst || (wst == WarmupState::Warming && rem_sec != last_rem_sec)) {
                last_wst = wst;
                last_rem_sec = rem_sec;

                if (wst == WarmupState::Cold) {
                    lv_obj_set_style_border_color(g_warmup_badge, lv_color_hex(0x0284C7), 0);
                    lv_obj_set_style_bg_color(g_warmup_badge, lv_color_hex(0x081524), 0);
                    lv_obj_set_style_text_color(g_warmup_lbl, lv_color_hex(0x38BDF8), 0);

                    if (g_warmup_icon) {
                        if (abarth::storage::sd().isMounted()) {
                            lv_image_set_src(g_warmup_icon, "S:/icons/oil_blue.png");
                        } else {
                            lv_image_set_src(g_warmup_icon, &g_oil_blue_img_dsc);
                        }
                        lv_image_set_scale(g_warmup_icon, 80);
                        lv_obj_set_style_image_recolor_opa(g_warmup_icon, LV_OPA_TRANSP, 0);
                        lv_obj_clear_flag(g_warmup_icon, LV_OBJ_FLAG_HIDDEN);
                    }

                    const auto s_coolant = abarth::data::store().get(Metric::CoolantTempC);
                    char b[32];
                    if (s_coolant.valid()) {
                        std::snprintf(b, sizeof(b), "H2O %.0f°C", s_coolant.value);
                    } else {
                        std::snprintf(b, sizeof(b), "MOTORE FREDDO");
                    }
                    lv_label_set_text(g_warmup_lbl, b);
                } else if (wst == WarmupState::Warming) {
                    lv_obj_set_style_border_color(g_warmup_badge, lv_color_hex(0xD97706), 0);
                    lv_obj_set_style_bg_color(g_warmup_badge, lv_color_hex(0x241406), 0);
                    lv_obj_set_style_text_color(g_warmup_lbl, lv_color_hex(0xF59E0B), 0);

                    if (abarth::storage::sd().isMounted() && g_warmup_icon) {
                        lv_image_set_src(g_warmup_icon, "S:/icons/timer.png");
                        lv_image_set_scale(g_warmup_icon, 80);
                        lv_obj_set_style_image_recolor_opa(g_warmup_icon, LV_OPA_TRANSP, 0);
                        lv_obj_clear_flag(g_warmup_icon, LV_OBJ_FLAG_HIDDEN);
                    }

                    char b[32];
                    std::snprintf(b, sizeof(b), "WARMUP %02d:%02d", rem_sec / 60, rem_sec % 60);
                    lv_label_set_text(g_warmup_lbl, b);
                } else if (wst == WarmupState::Ready) {
                    lv_obj_set_style_border_color(g_warmup_badge, lv_color_hex(0x16A34A), 0);
                    lv_obj_set_style_bg_color(g_warmup_badge, lv_color_hex(0x081C10), 0);
                    lv_obj_set_style_text_color(g_warmup_lbl, lv_color_hex(0x22C55E), 0);

                    if (abarth::storage::sd().isMounted() && g_warmup_icon) {
                        lv_image_set_src(g_warmup_icon, "S:/icons/checkered_flag.png");
                        lv_image_set_scale(g_warmup_icon, 80);
                        lv_obj_set_style_image_recolor_opa(g_warmup_icon, LV_OPA_TRANSP, 0);
                        lv_obj_clear_flag(g_warmup_icon, LV_OBJ_FLAG_HIDDEN);
                    }

                    lv_label_set_text(g_warmup_lbl, "OLIO PRONTO");
                }
            }
        }
    }

    // Traccia: stampiamo ogni 20 tick (= 2 s a 10 Hz) per capire se il
    // timer UI si ferma prima del reset HP_WDT.
    static uint32_t tick_count = 0;
    if ((tick_count++ % 20) == 0) {
        ESP_LOGD(TAG, "[TICK] #%lu uptime=%lldms",
                 static_cast<unsigned long>(tick_count - 1),
                 static_cast<long long>(now_ms));
    }


    // Aggiornamento stato MicroSD e logging telemetria (1 Hz)
    static int64_t s_last_sd_log_ms = 0;
    if (now_ms - s_last_sd_log_ms >= 1000) {
        s_last_sd_log_ms = now_ms;
        const bool sd_ok = abarth::storage::sd().isMounted();
        if (g_sd_label) {
            static int s_last_sd_ok = -1;
            if ((int)sd_ok != s_last_sd_ok) {
                s_last_sd_ok = (int)sd_ok;
                lv_label_set_text(g_sd_label, sd_ok ? LV_SYMBOL_SD_CARD : "");
            }
        }
        // Registra su SD solo durante polling attivo (evita scritture inutili a cruscotto fermo)
        if (sd_ok && abarth::obd::obd().state() == abarth::obd::ObdState::Polling) {
            auto& ds = abarth::data::store();
            char csv_buf[160];
            float rpm = ds.get(Metric::Rpm).value;
            float spd = ds.get(Metric::Speed).value;
            float bst = ds.get(Metric::BoostRelBar).value;
            float clt = ds.get(Metric::CoolantTempC).value;
            float oil = abarth::warmup::warmup_mgr().estimatedOilTemp();
            float iat = ds.get(Metric::IntakeAirTempC).value;
            float thr = ds.get(Metric::Throttle).value;

            std::snprintf(csv_buf, sizeof(csv_buf), "%lld,%.0f,%.1f,%.2f,%.1f,%.1f,%.1f,%.1f",
                          static_cast<long long>(now_ms), rpm, spd, bst, clt, oil, iat, thr);
            abarth::storage::sd().logTelemetry(csv_buf);
        }
    }

    // Header e Tab Stato: writeback solo se cambia stringa/valore.
    if (g_status_state_label || g_state_label || g_ok_counter_label || g_err_counter_label || g_device_label) {
        static abarth::obd::ObdState last_st = static_cast<abarth::obd::ObdState>(255);
        static uint32_t last_ok = UINT32_MAX, last_err = UINT32_MAX;
        static std::string last_dev_str = "";

        // 1. Device Info Label
        const auto& settings = abarth::settings::current();
        const bool is_can = (settings.connection_mode == abarth::settings::ConnectionMode::DirectCan);

        if (g_device_label) {
            std::string dev_str = "Dispositivo: ";
            if (is_can) {
                dev_str += "Dual CAN (WeAct ISOCANFD V1)";
            } else if (!settings.saved_dongle_mac.empty()) {
                dev_str += "BLE (" + settings.saved_dongle_mac + ")";
            } else if (!settings.saved_wifi_ssid.empty()) {
                dev_str += "WiFi (" + settings.saved_wifi_ssid + ")";
            } else {
                dev_str += "BLE / WiFi (Ricerca Automatica)";
            }
            if (dev_str != last_dev_str) {
                last_dev_str = dev_str;
                lv_label_set_text(g_device_label, dev_str.c_str());
            }
        }

        // 2. State Labels & OBD Connection Badge
        const bool is_demo = settings.demo_mode;
        const abarth::obd::ObdState st = abarth::obd::obd().state();
        const abarth::can::CanState can1_st = abarth::can::can().state(0);
        const abarth::can::CanState can2_st = abarth::can::can().state(1);

        if (st != last_st || is_demo || is_can) {
            last_st = st;
            char can_st_buf[64];
            if (is_can) {
                const bool c1_ok = (can1_st == abarth::can::CanState::Running);
                const bool c2_ok = (can2_st == abarth::can::CanState::Running);
                if (c1_ok && c2_ok) {
                    snprintf(can_st_buf, sizeof(can_st_buf), "CAN DUAL ATTIVO (OK)");
                } else if (c1_ok) {
                    snprintf(can_st_buf, sizeof(can_st_buf), "C-CAN 500K ATTIVO (OK)");
                } else if (c2_ok) {
                    snprintf(can_st_buf, sizeof(can_st_buf), "B-CAN 50K ATTIVO (OK)");
                } else if (can1_st == abarth::can::CanState::Error || can2_st == abarth::can::CanState::Error ||
                           can1_st == abarth::can::CanState::BusOff || can2_st == abarth::can::CanState::BusOff) {
                    snprintf(can_st_buf, sizeof(can_st_buf), "CAN BUS-OFF / ERRORE");
                } else {
                    snprintf(can_st_buf, sizeof(can_st_buf), "CAN ARRESTATO");
                }
            }
            const char* st_name = is_demo ? "[ SIMULAZIONE ]" : 
                                  is_can ? can_st_buf : 
                                  abarth::obd::stateName(st);
            if (g_status_state_label) {
                lv_label_set_text(g_status_state_label, st_name);
                lv_color_t color = is_demo ? lv_color_hex(0xFFD700) : lv_color_hex(0x9A9AA6);
                if (is_can) {
                    const bool c1_ok = (can1_st == abarth::can::CanState::Running);
                    const bool c2_ok = (can2_st == abarth::can::CanState::Running);
                    if (c1_ok || c2_ok) {
                        color = lv_color_hex(0x30CC60);
                    } else if (can1_st == abarth::can::CanState::BusOff || can2_st == abarth::can::CanState::BusOff ||
                               can1_st == abarth::can::CanState::Error || can2_st == abarth::can::CanState::Error) {
                        color = lv_color_hex(0xFF5252);
                    } else if (can1_st == abarth::can::CanState::Starting || can2_st == abarth::can::CanState::Starting ||
                               can1_st == abarth::can::CanState::Recovering || can2_st == abarth::can::CanState::Recovering) {
                        color = lv_color_hex(0xFFC107);
                    } else {
                        color = lv_color_hex(0x9A9AA6);
                    }
                } else if (!is_demo) {
                    switch (st) {
                        case abarth::obd::ObdState::Polling:
                            color = lv_color_hex(0x30CC60);
                            break;
                        case abarth::obd::ObdState::Connecting:
                        case abarth::obd::ObdState::Initializing:
                        case abarth::obd::ObdState::Scanning:
                            color = lv_color_hex(0xFFC107);
                            break;
                        case abarth::obd::ObdState::Error:
                            color = lv_color_hex(0xFF5252);
                            break;
                        default:
                            color = lv_color_hex(0x9A9AA6);
                            break;
                    }
                }
                lv_obj_set_style_text_color(g_status_state_label, color, 0);
            }
        }

        // Connection Badge in Header: animated pulsing/blinking icon + color badge
        if (g_conn_badge) {
            static abarth::obd::ObdState badge_last_st = static_cast<abarth::obd::ObdState>(255);
            static abarth::can::CanState badge_last_c1_st = static_cast<abarth::can::CanState>(255);
            static abarth::can::CanState badge_last_c2_st = static_cast<abarth::can::CanState>(255);
            static bool badge_last_demo = false;
            static bool badge_last_is_can = false;
            static uint8_t last_blink_phase = 255;

            const bool is_searching = (!is_demo) && (!is_can) && (st == abarth::obd::ObdState::Scanning ||
                                                                  st == abarth::obd::ObdState::Connecting ||
                                                                  st == abarth::obd::ObdState::Initializing);
            const uint8_t blink_phase = is_searching ? static_cast<uint8_t>((now_ms / 500) % 2) : 0;

            if (st != badge_last_st || can1_st != badge_last_c1_st || can2_st != badge_last_c2_st ||
                is_can != badge_last_is_can || is_demo != badge_last_demo || (is_searching && blink_phase != last_blink_phase)) {
                badge_last_st = st;
                badge_last_c1_st = can1_st;
                badge_last_c2_st = can2_st;
                badge_last_is_can = is_can;
                badge_last_demo = is_demo;
                last_blink_phase = blink_phase;

                if (is_demo) {
                    lv_obj_set_style_border_color(g_conn_badge, lv_color_hex(0x8B5CF6), 0);
                    lv_obj_set_style_bg_color(g_conn_badge, lv_color_hex(0x1E1035), 0);
                    lv_obj_set_style_opa(g_conn_badge, LV_OPA_COVER, 0);
                    if (g_conn_icon) {
                        lv_label_set_text(g_conn_icon, LV_SYMBOL_SETTINGS);
                        lv_obj_set_style_text_color(g_conn_icon, lv_color_hex(0xC084FC), 0);
                    }
                    if (g_conn_label) {
                        lv_label_set_text(g_conn_label, "DEMO");
                        lv_obj_set_style_text_color(g_conn_label, lv_color_hex(0xC084FC), 0);
                    }
                } else if (is_can) {
                    const bool c1_ok = (can1_st == abarth::can::CanState::Running);
                    const bool c2_ok = (can2_st == abarth::can::CanState::Running);
                    const bool any_err = (can1_st == abarth::can::CanState::BusOff || can1_st == abarth::can::CanState::Error ||
                                          can2_st == abarth::can::CanState::BusOff || can2_st == abarth::can::CanState::Error);

                    if (any_err && !c1_ok && !c2_ok) {
                        lv_obj_set_style_border_color(g_conn_badge, lv_color_hex(0xEF4444), 0);
                        lv_obj_set_style_bg_color(g_conn_badge, lv_color_hex(0x2A0E10), 0);
                        lv_obj_set_style_opa(g_conn_badge, LV_OPA_COVER, 0);
                        if (g_conn_icon) {
                            lv_label_set_text(g_conn_icon, LV_SYMBOL_WARNING);
                            lv_obj_set_style_text_color(g_conn_icon, lv_color_hex(0xF87171), 0);
                        }
                        if (g_conn_label) {
                            lv_label_set_text(g_conn_label, "CAN ERR");
                            lv_obj_set_style_text_color(g_conn_label, lv_color_hex(0xF87171), 0);
                        }
                    } else if (c1_ok || c2_ok) {
                        lv_obj_set_style_border_color(g_conn_badge, lv_color_hex(0x10B981), 0);
                        lv_obj_set_style_bg_color(g_conn_badge, lv_color_hex(0x042012), 0);
                        lv_obj_set_style_opa(g_conn_badge, LV_OPA_COVER, 0);
                        if (g_conn_icon) {
                            lv_label_set_text(g_conn_icon, LV_SYMBOL_SETTINGS);
                            lv_obj_set_style_text_color(g_conn_icon, lv_color_hex(0x34D399), 0);
                        }
                        if (g_conn_label) {
                            const char* badge_txt = (c1_ok && c2_ok) ? "CAN DUAL" :
                                                    c1_ok ? "C-CAN 500K" : "B-CAN 50K";
                            lv_label_set_text(g_conn_label, badge_txt);
                            lv_obj_set_style_text_color(g_conn_label, lv_color_hex(0x34D399), 0);
                        }
                    } else {
                        lv_obj_set_style_border_color(g_conn_badge, lv_color_hex(0xF59E0B), 0);
                        lv_obj_set_style_bg_color(g_conn_badge, lv_color_hex(0x261905), 0);
                        lv_obj_set_style_opa(g_conn_badge, LV_OPA_COVER, 0);
                        if (g_conn_icon) {
                            lv_label_set_text(g_conn_icon, LV_SYMBOL_SETTINGS);
                            lv_obj_set_style_text_color(g_conn_icon, lv_color_hex(0xFBBF24), 0);
                        }
                        if (g_conn_label) {
                            lv_label_set_text(g_conn_label, "CAN REC");
                            lv_obj_set_style_text_color(g_conn_label, lv_color_hex(0xFBBF24), 0);
                        }
                    }
                } else if (st == abarth::obd::ObdState::Polling) {
                    lv_obj_set_style_border_color(g_conn_badge, lv_color_hex(0x10B981), 0);
                    lv_obj_set_style_bg_color(g_conn_badge, lv_color_hex(0x042012), 0);
                    lv_obj_set_style_opa(g_conn_badge, LV_OPA_COVER, 0);
                    if (g_conn_icon) {
                        lv_label_set_text(g_conn_icon, LV_SYMBOL_BLUETOOTH);
                        lv_obj_set_style_text_color(g_conn_icon, lv_color_hex(0x34D399), 0);
                    }
                    if (g_conn_label) {
                        lv_label_set_text(g_conn_label, "OBD OK");
                        lv_obj_set_style_text_color(g_conn_label, lv_color_hex(0x34D399), 0);
                    }
                } else if (is_searching) {
                    lv_obj_set_style_border_color(g_conn_badge, lv_color_hex(0xF59E0B), 0);
                    lv_obj_set_style_bg_color(g_conn_badge, lv_color_hex(0x261905), 0);
                    // Pulsing/blinking animation at 1 Hz
                    lv_obj_set_style_opa(g_conn_badge, (blink_phase == 0) ? LV_OPA_COVER : LV_OPA_30, 0);
                    if (g_conn_icon) {
                        lv_label_set_text(g_conn_icon, LV_SYMBOL_BLUETOOTH);
                        lv_obj_set_style_text_color(g_conn_icon, lv_color_hex(0xFBBF24), 0);
                    }
                    if (g_conn_label) {
                        const char* txt = (st == abarth::obd::ObdState::Scanning) ? "SCAN" :
                                          (st == abarth::obd::ObdState::Connecting) ? "CONN" : "INIT";
                        lv_label_set_text(g_conn_label, txt);
                        lv_obj_set_style_text_color(g_conn_label, lv_color_hex(0xFBBF24), 0);
                    }
                } else if (st == abarth::obd::ObdState::Error) {
                    lv_obj_set_style_border_color(g_conn_badge, lv_color_hex(0xEF4444), 0);
                    lv_obj_set_style_bg_color(g_conn_badge, lv_color_hex(0x2A0E10), 0);
                    lv_obj_set_style_opa(g_conn_badge, LV_OPA_COVER, 0);
                    if (g_conn_icon) {
                        lv_label_set_text(g_conn_icon, LV_SYMBOL_WARNING);
                        lv_obj_set_style_text_color(g_conn_icon, lv_color_hex(0xF87171), 0);
                    }
                    if (g_conn_label) {
                        lv_label_set_text(g_conn_label, "ERR");
                        lv_obj_set_style_text_color(g_conn_label, lv_color_hex(0xF87171), 0);
                    }
                } else { // Disconnected / idle
                    lv_obj_set_style_border_color(g_conn_badge, lv_color_hex(0x3F3F46), 0);
                    lv_obj_set_style_bg_color(g_conn_badge, lv_color_hex(0x141416), 0);
                    lv_obj_set_style_opa(g_conn_badge, LV_OPA_70, 0);
                    if (g_conn_icon) {
                        lv_label_set_text(g_conn_icon, LV_SYMBOL_BLUETOOTH);
                        lv_obj_set_style_text_color(g_conn_icon, lv_color_hex(0x71717A), 0);
                    }
                    if (g_conn_label) {
                        lv_label_set_text(g_conn_label, "OFFLINE");
                        lv_obj_set_style_text_color(g_conn_label, lv_color_hex(0x71717A), 0);
                    }
                }
            }
        }

        // 3. Counters & Last Error
        const uint32_t ok  = is_can ? abarth::can::can().totalRxFrames() : abarth::obd::obd().successfulReads();
        const uint32_t err = is_can ? abarth::can::can().totalErrors() : abarth::obd::obd().failedReads();
        if (ok != last_ok || err != last_err) {
            last_ok = ok; last_err = err;
            if (g_ok_counter_label) {
                char tmp[32];
                std::snprintf(tmp, sizeof(tmp), "%lu", static_cast<unsigned long>(ok));
                lv_label_set_text(g_ok_counter_label, tmp);
            }
            if (g_err_counter_label) {
                char tmp[32];
                std::snprintf(tmp, sizeof(tmp), "%lu", static_cast<unsigned long>(err));
                lv_label_set_text(g_err_counter_label, tmp);
            }
            if (g_counters_label) {
                char tmp[64];
                std::snprintf(tmp, sizeof(tmp), "OK %lu  /  ERR %lu",
                              static_cast<unsigned long>(ok),
                              static_cast<unsigned long>(err));
                lv_label_set_text(g_counters_label, tmp);
            }
            if (g_last_err_label) {
                std::string err_str;
                if (is_can) {
                    if (!abarth::can::can().lastError(0).empty()) {
                        err_str = "CAN1: " + abarth::can::can().lastError(0);
                    } else if (!abarth::can::can().lastError(1).empty()) {
                        err_str = "CAN2: " + abarth::can::can().lastError(1);
                    }
                } else {
                    err_str = abarth::obd::obd().lastError();
                }
                if (!err_str.empty()) {
                    lv_label_set_text(g_last_err_label, err_str.c_str());
                    lv_obj_set_style_text_color(g_last_err_label, lv_color_hex(0xFF5252), 0);
                } else {
                    lv_label_set_text(g_last_err_label, "Nessun errore rilevato");
                    lv_obj_set_style_text_color(g_last_err_label, lv_color_hex(0x9A9AA6), 0);
                }
            }
        }
    }
    update_header_clock();

    settings_tab::tick();

    // === MINIMAL FUNZIONALE: aggiorna cockpit e telemetry strips solo se visibili (tab 0..5) ===
    const uint32_t active_tab = g_main_tabview ? lv_tabview_get_tab_active(g_main_tabview) : 0;
    if (active_tab <= 5 && g_mf_dash.active) {
        update_minimal_dashboard(now_ms);
    }
}

/* ==========================================================================
 *  MINIMAL FUNZIONALE — Dashboard Essenziale ad Alta Leggibilita'
 * ========================================================================== */

struct MetricMeta {
    const char* title;
    const char* unit;
    float       min_v;
    float       max_v;
    int         decimals;
    ValueKind   kind;
    lv_color_t  color;
};

static MetricMeta get_metric_meta(Metric m) {
    switch (m) {
        case Metric::Rpm:
            return {"Giri Motore", "RPM", 0, 7000, 0, ValueKind::Generic, lv_color_hex(mf::ACCENT_GREEN)};
        case Metric::Speed:
            return {"Velocita' Veicolo", settings::speedUnitSymbol(), 0, 260, 0, ValueKind::SpeedKmh, lv_color_hex(mf::ACCENT_RED)};
        case Metric::EngineLoad:
            return {"Carico Motore", "%", 0, 100, 0, ValueKind::Generic, lv_color_hex(mf::ACCENT_AMBER)};
        case Metric::Throttle:
            return {"Posizione Farfalla", "%", 0, 100, 0, ValueKind::Generic, lv_color_hex(mf::ACCENT_CYAN)};
        case Metric::CoolantTempC:
            return {"Liquido Raffreddamento", settings::tempUnitSymbol(), 40, 130, 0, ValueKind::TempC, lv_color_hex(mf::ACCENT_AMBER)};
        case Metric::IntakeAirTempC:
            return {"Aria Aspirata (IAT)", settings::tempUnitSymbol(), -10, 80, 0, ValueKind::TempC, lv_color_hex(mf::ACCENT_CYAN)};
        case Metric::AmbientTempC:
            return {"Temperatura Esterna", settings::tempUnitSymbol(), -20, 50, 0, ValueKind::TempC, lv_color_hex(mf::TEXT_MUTED)};
        case Metric::OilTempC:
            return {"Olio Motore", settings::tempUnitSymbol(), 50, 150, 0, ValueKind::TempC, lv_color_hex(mf::ACCENT_RED)};
        case Metric::EgtC:
            return {"Gas Scarico (EGT)", settings::tempUnitSymbol(), 100, 950, 0, ValueKind::TempC, lv_color_hex(mf::ACCENT_RED)};
        case Metric::BoostKpa:
            return {"Pressione Collettore (MAP)", settings::pressureUnitSymbol(), 20, 260, 0, ValueKind::PressureKpa, lv_color_hex(mf::ACCENT_AMBER)};
        case Metric::BoostRelBar:
            return {"Pressione Turbo (Boost)", "bar", -0.5f, 2.5f, 2, ValueKind::Generic, lv_color_hex(mf::ACCENT_RED)};
        case Metric::OilPressureBar:
            return {"Pressione Olio", "bar", 0.0f, 7.0f, 1, ValueKind::Generic, lv_color_hex(mf::ACCENT_CYAN)};
        case Metric::BatteryVolts:
            return {"Tensione Batteria", "V", 9.0f, 16.0f, 2, ValueKind::Generic, lv_color_hex(mf::ACCENT_GREEN)};
        case Metric::Afr:
            return {"Rapporto Stechiometrico (AFR)", "", 10.0f, 20.0f, 2, ValueKind::Generic, lv_color_hex(mf::ACCENT_CYAN)};
        case Metric::FuelLevelPct:
            return {"Livello Serbatoio", "%", 0, 100, 0, ValueKind::Generic, lv_color_hex(mf::ACCENT_GREEN)};
        case Metric::FuelPressureKpa:
            return {"Rail Carburante", settings::pressureUnitSymbol(), 0, 16000, 0, ValueKind::PressureKpa, lv_color_hex(mf::ACCENT_GREEN)};
        case Metric::FuelRateLh:
            return {"Portata Carburante", "L/h", 0, 50, 2, ValueKind::Generic, lv_color_hex(mf::ACCENT_CYAN)};
        case Metric::FuelConsumptionL100:
            return {"Consumo Istantaneo", "L/100", 0, 30, 1, ValueKind::Generic, lv_color_hex(mf::ACCENT_AMBER)};
        case Metric::FuelTempC:
            return {"Temperatura Carburante", settings::tempUnitSymbol(), 0, 80, 0, ValueKind::TempC, lv_color_hex(mf::ACCENT_AMBER)};
        case Metric::FuelInjectionTiming:
            return {"Anticipo Iniezione", "deg", -10, 50, 1, ValueKind::Generic, lv_color_hex(mf::ACCENT_CYAN)};
        case Metric::EthanolPct:
            return {"Percentuale Etanolo", "%", 0, 100, 0, ValueKind::Generic, lv_color_hex(mf::ACCENT_GREEN)};
        case Metric::DistanceSinceClear:
            return {"Km da Reset DTC", "km", 0, 1000, 0, ValueKind::Generic, lv_color_hex(mf::TEXT_MUTED)};
        default:
            return {"Sensore", "", 0, 100, 0, ValueKind::Generic, lv_color_hex(mf::TEXT_WHITE)};
    }
}

static MinimalStrip* add_strip_internal(lv_obj_t* parent, Metric m, const char* title,
                                        const char* unit, float min_v, float max_v,
                                        int decimals, ValueKind kind,
                                        lv_color_t accent_color, bool is_custom) {
    MinimalStrip* s_ptr = nullptr;
    if (is_custom) {
        if (g_custom_strip_count >= g_custom_strips.size()) return nullptr;
        s_ptr = &g_custom_strips[g_custom_strip_count++];
    } else {
        if (g_mf_strip_count >= g_mf_strips.size()) return nullptr;
        s_ptr = &g_mf_strips[g_mf_strip_count++];
    }

    MinimalStrip& s = *s_ptr;
    const bool is_portrait = (LV_HOR_RES < LV_VER_RES);

    s.metric   = m;
    s.kind     = kind;
    s.decimals = decimals;
    s.unit_str = unit;
    s.min_val  = min_v;
    s.max_val  = max_v;
    s.color    = accent_color;
    s.prev_text[0] = '\0';
    s.last_bar_val = -9999;
    s.muted    = true;

    // Row container (ingrandito per visibilita' in vettura)
    lv_obj_t* row = lv_obj_create(parent);
    s.row = row;
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), is_portrait ? 84 : 80);
    lv_obj_set_style_bg_color(row, lv_color_hex(mf::SURFACE_DARK), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(row, 10, 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_border_color(row, lv_color_hex(mf::SURFACE_BORDER), 0);
    lv_obj_set_style_pad_hor(row, is_portrait ? 14 : 20, 0);
    lv_obj_set_style_pad_ver(row, 8, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Sinistra: Titolo parametro (ampio e chiaro per colpo d'occhio rapido)
    lv_obj_t* left_box = lv_obj_create(row);
    lv_obj_remove_style_all(left_box);
    lv_obj_set_size(left_box, is_portrait ? 170 : 260, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(left_box, LV_FLEX_FLOW_COLUMN);

    lv_obj_t* t_lbl = lv_label_create(left_box);
    lv_label_set_text(t_lbl, title);
    lv_obj_set_style_text_color(t_lbl, lv_color_hex(0xE2E8F0), 0);
    lv_obj_set_style_text_font(t_lbl, is_portrait ? &lv_font_montserrat_14 : &lv_font_montserrat_18, 0);

    // Centro: Barra orizzontale spessa ad alto contrasto
    lv_obj_t* center_box = lv_obj_create(row);
    lv_obj_remove_style_all(center_box);
    lv_obj_set_flex_grow(center_box, 1);
    lv_obj_set_height(center_box, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_hor(center_box, is_portrait ? 12 : 22, 0);
    lv_obj_set_flex_flow(center_box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(center_box, 5, 0);

    s.bar = lv_bar_create(center_box);
    lv_obj_set_size(s.bar, LV_PCT(100), 12);
    lv_bar_set_range(s.bar, 0, 1000);
    lv_bar_set_value(s.bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s.bar, lv_color_hex(mf::BAR_TRACK), LV_PART_MAIN);
    lv_obj_set_style_radius(s.bar, 6, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s.bar, accent_color, LV_PART_INDICATOR);
    lv_obj_set_style_radius(s.bar, 6, LV_PART_INDICATOR);

    // Range min/max sotto la barra
    lv_obj_t* r_box = lv_obj_create(center_box);
    lv_obj_remove_style_all(r_box);
    lv_obj_set_size(r_box, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(r_box, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r_box, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    char min_s[16], max_s[16];
    std::snprintf(min_s, sizeof(min_s), "%.0f", min_v);
    std::snprintf(max_s, sizeof(max_s), "%.0f", max_v);
    lv_obj_t* l_min = lv_label_create(r_box);
    lv_label_set_text(l_min, min_s);
    lv_obj_set_style_text_color(l_min, lv_color_hex(mf::TEXT_DIM), 0);
    lv_obj_set_style_text_font(l_min, &lv_font_montserrat_12, 0);

    lv_obj_t* l_max = lv_label_create(r_box);
    lv_label_set_text(l_max, max_s);
    lv_obj_set_style_text_color(l_max, lv_color_hex(mf::TEXT_DIM), 0);
    lv_obj_set_style_text_font(l_max, &lv_font_montserrat_12, 0);

    // Destra: Grande Valore Digitale e Unita'
    lv_obj_t* right_box = lv_obj_create(row);
    lv_obj_remove_style_all(right_box);
    lv_obj_set_size(right_box, is_portrait ? 120 : 160, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(right_box, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(right_box, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(right_box, 8, 0);

    s.val_label = lv_label_create(right_box);
    lv_label_set_text(s.val_label, "--");
    lv_obj_set_style_text_color(s.val_label, lv_color_hex(mf::TEXT_MUTED), 0);
    lv_obj_set_style_text_font(s.val_label, is_portrait ? &lv_font_montserrat_28 : &lv_font_montserrat_34, 0);

    s.unit_label = lv_label_create(right_box);
    lv_label_set_text(s.unit_label, unit);
    lv_obj_set_style_text_color(s.unit_label, lv_color_hex(mf::ACCENT_RED), 0);
    lv_obj_set_style_text_font(s.unit_label, is_portrait ? &lv_font_montserrat_16 : &lv_font_montserrat_18, 0);

    return s_ptr;
}

void add_minimal_strip(lv_obj_t* parent, Metric m, const char* title,
                       const char* unit, float min_v, float max_v,
                       int decimals = 1, ValueKind kind = ValueKind::Generic,
                       lv_color_t accent_color = lv_color_hex(mf::ACCENT_RED)) {
    add_strip_internal(parent, m, title, unit, min_v, max_v, decimals, kind, accent_color, false);
}

void add_custom_strip(lv_obj_t* parent, Metric m, const char* title,
                      const char* unit, float min_v, float max_v,
                      int decimals = 1, ValueKind kind = ValueKind::Generic,
                      lv_color_t accent_color = lv_color_hex(mf::ACCENT_RED)) {
    add_strip_internal(parent, m, title, unit, min_v, max_v, decimals, kind, accent_color, true);
}

void build_minimal_cockpit_tab(lv_obj_t* tab) {
    lv_obj_set_style_bg_opa(tab, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(tab, 8, 0);
    lv_obj_set_style_pad_row(tab, 8, 0);
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_COLUMN);
    const bool is_portrait = (LV_HOR_RES < LV_VER_RES);

    // 1. TOP ROW: Progressive Rev Bar & Shift Indicator (Altezza e font maggiorati per guida)
    lv_obj_t* rev_card = lv_obj_create(tab);
    g_mf_dash.rev_card = rev_card;
    lv_obj_remove_style_all(rev_card);
    lv_obj_set_size(rev_card, LV_PCT(100), is_portrait ? 64 : 60);
    lv_obj_set_style_bg_color(rev_card, lv_color_hex(mf::SURFACE_DARK), 0);
    lv_obj_set_style_bg_opa(rev_card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(rev_card, 10, 0);
    lv_obj_set_style_border_width(rev_card, 1, 0);
    lv_obj_set_style_border_color(rev_card, lv_color_hex(mf::SURFACE_BORDER), 0);
    lv_obj_set_style_pad_hor(rev_card, 16, 0);
    lv_obj_set_style_pad_ver(rev_card, 8, 0);
    lv_obj_set_flex_flow(rev_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(rev_card, 6, 0);

    lv_obj_t* rev_info = lv_obj_create(rev_card);
    lv_obj_remove_style_all(rev_info);
    lv_obj_set_size(rev_info, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(rev_info, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(rev_info, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* l_tach = lv_label_create(rev_info);
    lv_label_set_text(l_tach, "TACHIMETRO MOTORE");
    lv_obj_set_style_text_color(l_tach, lv_color_hex(mf::TEXT_DIM), 0);
    lv_obj_set_style_text_font(l_tach, &lv_font_montserrat_14, 0);

    lv_obj_t* rev_val_box = lv_obj_create(rev_info);
    lv_obj_remove_style_all(rev_val_box);
    lv_obj_set_size(rev_val_box, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(rev_val_box, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(rev_val_box, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(rev_val_box, 12, 0);

    g_mf_dash.shift_light = lv_label_create(rev_val_box);
    lv_label_set_text(g_mf_dash.shift_light, LV_SYMBOL_UP " SHIFT");
    lv_obj_set_style_text_color(g_mf_dash.shift_light, lv_color_hex(mf::ACCENT_RED), 0);
    lv_obj_set_style_text_font(g_mf_dash.shift_light, &lv_font_montserrat_18, 0);
    lv_obj_add_flag(g_mf_dash.shift_light, LV_OBJ_FLAG_HIDDEN);

    g_mf_dash.rev_label = lv_label_create(rev_val_box);
    lv_label_set_text(g_mf_dash.rev_label, "--- RPM");
    lv_obj_set_style_text_color(g_mf_dash.rev_label, lv_color_hex(mf::TEXT_WHITE), 0);
    lv_obj_set_style_text_font(g_mf_dash.rev_label, &lv_font_montserrat_22, 0);

    g_mf_dash.rev_bar = lv_bar_create(rev_card);
    lv_obj_set_size(g_mf_dash.rev_bar, LV_PCT(100), 10);
    lv_bar_set_range(g_mf_dash.rev_bar, 0, 7000);
    lv_bar_set_value(g_mf_dash.rev_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(g_mf_dash.rev_bar, lv_color_hex(mf::BAR_TRACK), LV_PART_MAIN);
    lv_obj_set_style_radius(g_mf_dash.rev_bar, 5, LV_PART_MAIN);
    lv_obj_set_style_bg_color(g_mf_dash.rev_bar, lv_color_hex(mf::ACCENT_GREEN), LV_PART_INDICATOR);
    lv_obj_set_style_radius(g_mf_dash.rev_bar, 5, LV_PART_INDICATOR);

    // 2. MIDDLE SECTION: Speedometer Hero + Engine Load & Throttle
    lv_obj_t* mid_row = lv_obj_create(tab);
    g_mf_dash.mid_row = mid_row;
    lv_obj_remove_style_all(mid_row);
    lv_obj_set_size(mid_row, LV_PCT(100), is_portrait ? LV_SIZE_CONTENT : 225);
    lv_obj_set_flex_flow(mid_row, is_portrait ? LV_FLEX_FLOW_COLUMN : LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(mid_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(mid_row, 8, 0);

    // 2A. Speedometer Hero Box
    lv_obj_t* spd_card = lv_obj_create(mid_row);
    g_mf_dash.spd_card = spd_card;
    lv_obj_remove_style_all(spd_card);
    if (is_portrait) {
        lv_obj_set_size(spd_card, LV_PCT(100), 175);
    } else {
        lv_obj_set_size(spd_card, LV_PCT(62), LV_PCT(100));
    }
    lv_obj_set_style_bg_color(spd_card, lv_color_hex(mf::SURFACE_DARK), 0);
    lv_obj_set_style_bg_opa(spd_card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(spd_card, 12, 0);
    lv_obj_set_style_border_width(spd_card, 1, 0);
    lv_obj_set_style_border_color(spd_card, lv_color_hex(mf::SURFACE_BORDER), 0);
    lv_obj_set_flex_flow(spd_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(spd_card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(spd_card, 10, 0);
    lv_obj_set_style_pad_row(spd_card, 6, 0);

    lv_obj_t* l_spd_title = lv_label_create(spd_card);
    lv_label_set_text(l_spd_title, "VELOCITA'");
    lv_obj_set_style_text_color(l_spd_title, lv_color_hex(mf::TEXT_DIM), 0);
    lv_obj_set_style_text_font(l_spd_title, &lv_font_montserrat_14, 0);

    // Row per numero velocita' + unita'
    lv_obj_t* spd_val_box = lv_obj_create(spd_card);
    lv_obj_remove_style_all(spd_val_box);
    lv_obj_set_size(spd_val_box, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(spd_val_box, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(spd_val_box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(spd_val_box, 8, 0);

    g_mf_dash.speed_label = lv_label_create(spd_val_box);
    lv_label_set_text(g_mf_dash.speed_label, "--");
    lv_obj_set_style_text_color(g_mf_dash.speed_label, lv_color_hex(mf::TEXT_WHITE), 0);
    lv_obj_set_style_text_font(g_mf_dash.speed_label, &lv_font_montserrat_48, 0);

    g_mf_dash.speed_unit_label = lv_label_create(spd_val_box);
    lv_label_set_text(g_mf_dash.speed_unit_label, settings::speedUnitSymbol());
    lv_obj_set_style_text_color(g_mf_dash.speed_unit_label, lv_color_hex(mf::ACCENT_RED), 0);
    lv_obj_set_style_text_font(g_mf_dash.speed_unit_label, &lv_font_montserrat_20, 0);

    // Indicatore Marcia
    g_mf_dash.gear_label = lv_label_create(spd_card);
    lv_label_set_text(g_mf_dash.gear_label, "DISCONNESSO");
    lv_obj_set_style_text_color(g_mf_dash.gear_label, lv_color_hex(mf::TEXT_MUTED), 0);
    lv_obj_set_style_text_font(g_mf_dash.gear_label, &lv_font_montserrat_18, 0);

    // 2B. Right Box: Carico & Acceleratore (Valori e barre ingranditi)
    lv_obj_t* dyn_card = lv_obj_create(mid_row);
    g_mf_dash.dyn_card = dyn_card;
    lv_obj_remove_style_all(dyn_card);
    if (is_portrait) {
        lv_obj_set_size(dyn_card, LV_PCT(100), 140);
    } else {
        lv_obj_set_size(dyn_card, LV_PCT(36), LV_PCT(100));
    }
    lv_obj_set_style_bg_color(dyn_card, lv_color_hex(mf::SURFACE_DARK), 0);
    lv_obj_set_style_bg_opa(dyn_card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(dyn_card, 12, 0);
    lv_obj_set_style_border_width(dyn_card, 1, 0);
    lv_obj_set_style_border_color(dyn_card, lv_color_hex(mf::SURFACE_BORDER), 0);
    lv_obj_set_flex_flow(dyn_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(dyn_card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(dyn_card, 14, 0);
    lv_obj_set_style_pad_row(dyn_card, 8, 0);

    // Carico riga
    lv_obj_t* r_ld = lv_obj_create(dyn_card);
    g_mf_dash.r_ld = r_ld;
    lv_obj_remove_style_all(r_ld);
    lv_obj_set_size(r_ld, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(r_ld, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r_ld, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* l_ld_t = lv_label_create(r_ld);
    lv_label_set_text(l_ld_t, "CARICO");
    lv_obj_set_style_text_color(l_ld_t, lv_color_hex(mf::TEXT_DIM), 0);
    lv_obj_set_style_text_font(l_ld_t, &lv_font_montserrat_14, 0);

    g_mf_dash.load_label = lv_label_create(r_ld);
    lv_label_set_text(g_mf_dash.load_label, "--%");
    lv_obj_set_style_text_color(g_mf_dash.load_label, lv_color_hex(mf::TEXT_WHITE), 0);
    lv_obj_set_style_text_font(g_mf_dash.load_label, &lv_font_montserrat_22, 0);

    g_mf_dash.load_bar = lv_bar_create(dyn_card);
    lv_obj_set_size(g_mf_dash.load_bar, LV_PCT(100), 10);
    lv_bar_set_range(g_mf_dash.load_bar, 0, 100);
    lv_bar_set_value(g_mf_dash.load_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(g_mf_dash.load_bar, lv_color_hex(mf::BAR_TRACK), LV_PART_MAIN);
    lv_obj_set_style_radius(g_mf_dash.load_bar, 5, LV_PART_MAIN);
    lv_obj_set_style_bg_color(g_mf_dash.load_bar, lv_color_hex(mf::ACCENT_CYAN), LV_PART_INDICATOR);
    lv_obj_set_style_radius(g_mf_dash.load_bar, 5, LV_PART_INDICATOR);

    // Farfalla riga
    lv_obj_t* r_th = lv_obj_create(dyn_card);
    g_mf_dash.r_th = r_th;
    lv_obj_remove_style_all(r_th);
    lv_obj_set_size(r_th, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(r_th, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r_th, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* l_th_t = lv_label_create(r_th);
    lv_label_set_text(l_th_t, "ACCELERATORE");
    lv_obj_set_style_text_color(l_th_t, lv_color_hex(mf::TEXT_DIM), 0);
    lv_obj_set_style_text_font(l_th_t, &lv_font_montserrat_14, 0);

    g_mf_dash.throttle_label = lv_label_create(r_th);
    lv_label_set_text(g_mf_dash.throttle_label, "--%");
    lv_obj_set_style_text_color(g_mf_dash.throttle_label, lv_color_hex(mf::TEXT_WHITE), 0);
    lv_obj_set_style_text_font(g_mf_dash.throttle_label, &lv_font_montserrat_22, 0);

    g_mf_dash.throttle_bar = lv_bar_create(dyn_card);
    lv_obj_set_size(g_mf_dash.throttle_bar, LV_PCT(100), 10);
    lv_bar_set_range(g_mf_dash.throttle_bar, 0, 100);
    lv_bar_set_value(g_mf_dash.throttle_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(g_mf_dash.throttle_bar, lv_color_hex(mf::BAR_TRACK), LV_PART_MAIN);
    lv_obj_set_style_radius(g_mf_dash.throttle_bar, 5, LV_PART_MAIN);
    lv_obj_set_style_bg_color(g_mf_dash.throttle_bar, lv_color_hex(mf::ACCENT_AMBER), LV_PART_INDICATOR);
    lv_obj_set_style_radius(g_mf_dash.throttle_bar, 5, LV_PART_INDICATOR);

    // 3. BOTTOM SECTION: 4 Essential Telemetry Pillars (Pilastri maggiorati ad alta leggibilita')
    lv_obj_t* btm_row = lv_obj_create(tab);
    g_mf_dash.btm_row = btm_row;
    lv_obj_remove_style_all(btm_row);
    lv_obj_set_size(btm_row, LV_PCT(100), is_portrait ? LV_SIZE_CONTENT : 150);
    lv_obj_set_flex_flow(btm_row, is_portrait ? LV_FLEX_FLOW_ROW_WRAP : LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btm_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(btm_row, 8, 0);

    auto make_pillar = [is_portrait](lv_obj_t* parent, const char* title) -> lv_obj_t* {
        lv_obj_t* p = lv_obj_create(parent);
        lv_obj_remove_style_all(p);
        if (is_portrait) {
            lv_obj_set_size(p, LV_PCT(48), 135);
            lv_obj_set_flex_grow(p, 1);
        } else {
            lv_obj_set_flex_grow(p, 1);
            lv_obj_set_height(p, LV_PCT(100));
        }
        lv_obj_set_style_bg_color(p, lv_color_hex(mf::SURFACE_DARK), 0);
        lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(p, 10, 0);
        lv_obj_set_style_border_width(p, 1, 0);
        lv_obj_set_style_border_color(p, lv_color_hex(mf::SURFACE_BORDER), 0);
        lv_obj_set_flex_flow(p, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(p, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_set_style_pad_all(p, 12, 0);

        lv_obj_t* t = lv_label_create(p);
        lv_label_set_text(t, title);
        lv_obj_set_style_text_color(t, lv_color_hex(mf::TEXT_DIM), 0);
        lv_obj_set_style_text_font(t, &lv_font_montserrat_14, 0);
        return p;
    };

    // Pillar 1: BOOST
    lv_obj_t* p1 = make_pillar(btm_row, "TURBO BOOST");
    g_mf_dash.p1 = p1;
    g_mf_dash.p1_boost_val = lv_label_create(p1);
    lv_label_set_text(g_mf_dash.p1_boost_val, "-- bar");
    lv_obj_set_style_text_color(g_mf_dash.p1_boost_val, lv_color_hex(mf::TEXT_WHITE), 0);
    lv_obj_set_style_text_font(g_mf_dash.p1_boost_val, &lv_font_montserrat_28, 0);

    g_mf_dash.p1_boost_bar = lv_bar_create(p1);
    lv_obj_set_size(g_mf_dash.p1_boost_bar, LV_PCT(100), 8);
    lv_bar_set_range(g_mf_dash.p1_boost_bar, 0, 250);
    lv_bar_set_value(g_mf_dash.p1_boost_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(g_mf_dash.p1_boost_bar, lv_color_hex(mf::BAR_TRACK), LV_PART_MAIN);
    lv_obj_set_style_radius(g_mf_dash.p1_boost_bar, 4, LV_PART_MAIN);
    lv_obj_set_style_bg_color(g_mf_dash.p1_boost_bar, lv_color_hex(mf::ACCENT_RED), LV_PART_INDICATOR);
    lv_obj_set_style_radius(g_mf_dash.p1_boost_bar, 4, LV_PART_INDICATOR);

    // Pillar 2: COOLANT TEMP
    lv_obj_t* p2 = make_pillar(btm_row, "TEMP. LIQUIDO");
    g_mf_dash.p2 = p2;
    g_mf_dash.p2_coolant_val = lv_label_create(p2);
    lv_label_set_text(g_mf_dash.p2_coolant_val, "--");
    lv_obj_set_style_text_color(g_mf_dash.p2_coolant_val, lv_color_hex(mf::TEXT_WHITE), 0);
    lv_obj_set_style_text_font(g_mf_dash.p2_coolant_val, &lv_font_montserrat_28, 0);

    g_mf_dash.p2_coolant_badge = lv_label_create(p2);
    lv_label_set_text(g_mf_dash.p2_coolant_badge, "FREDDO");
    lv_obj_set_style_text_color(g_mf_dash.p2_coolant_badge, lv_color_hex(mf::TEXT_MUTED), 0);
    lv_obj_set_style_text_font(g_mf_dash.p2_coolant_badge, &lv_font_montserrat_14, 0);

    // Pillar 3: OIL TEMP
    lv_obj_t* p3 = make_pillar(btm_row, "TEMP. OLIO");
    g_mf_dash.p3 = p3;
    g_mf_dash.p3_oil_val = lv_label_create(p3);
    lv_label_set_text(g_mf_dash.p3_oil_val, "--");
    lv_obj_set_style_text_color(g_mf_dash.p3_oil_val, lv_color_hex(mf::TEXT_WHITE), 0);
    lv_obj_set_style_text_font(g_mf_dash.p3_oil_val, &lv_font_montserrat_28, 0);

    g_mf_dash.p3_oil_badge = lv_label_create(p3);
    lv_label_set_text(g_mf_dash.p3_oil_badge, "STANDBY");
    lv_obj_set_style_text_color(g_mf_dash.p3_oil_badge, lv_color_hex(mf::TEXT_MUTED), 0);
    lv_obj_set_style_text_font(g_mf_dash.p3_oil_badge, &lv_font_montserrat_14, 0);

    // Pillar 4: BATTERIA & AFR
    lv_obj_t* p4 = make_pillar(btm_row, "ELETTRICO / AFR");
    g_mf_dash.p4 = p4;
    g_mf_dash.p4_batt_val = lv_label_create(p4);
    lv_label_set_text(g_mf_dash.p4_batt_val, "-- V");
    lv_obj_set_style_text_color(g_mf_dash.p4_batt_val, lv_color_hex(mf::TEXT_WHITE), 0);
    lv_obj_set_style_text_font(g_mf_dash.p4_batt_val, &lv_font_montserrat_24, 0);

    g_mf_dash.p4_afr_val = lv_label_create(p4);
    lv_label_set_text(g_mf_dash.p4_afr_val, "AFR --");
    lv_obj_set_style_text_color(g_mf_dash.p4_afr_val, lv_color_hex(mf::TEXT_MUTED), 0);
    lv_obj_set_style_text_font(g_mf_dash.p4_afr_val, &lv_font_montserrat_18, 0);

    g_mf_dash.active = true;
}

void build_minimal_temp_tab(lv_obj_t* tab) {
    lv_obj_set_style_bg_opa(tab, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(tab, 8, 0);
    lv_obj_set_style_pad_row(tab, 8, 0);
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_COLUMN);

    add_minimal_strip(tab, Metric::CoolantTempC,   "Liquido Raffreddamento", "C", 40, 130, 0, ValueKind::TempC, lv_color_hex(mf::ACCENT_AMBER));
    add_minimal_strip(tab, Metric::OilTempC,        "Olio Motore",            "C", 50, 150, 0, ValueKind::TempC, lv_color_hex(mf::ACCENT_RED));
    add_minimal_strip(tab, Metric::IntakeAirTempC,  "Aria Aspirata (IAT)",    "C", -10, 80, 0, ValueKind::TempC, lv_color_hex(mf::ACCENT_CYAN));
    add_minimal_strip(tab, Metric::EgtC,            "Gas Scarico (EGT)",      "C", 100, 950,0, ValueKind::TempC, lv_color_hex(mf::ACCENT_RED));
    add_minimal_strip(tab, Metric::AmbientTempC,    "Temperatura Esterna",    "C", -20, 50, 0, ValueKind::TempC, lv_color_hex(mf::TEXT_MUTED));
}

void build_minimal_pressure_tab(lv_obj_t* tab) {
    lv_obj_set_style_bg_opa(tab, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(tab, 8, 0);
    lv_obj_set_style_pad_row(tab, 8, 0);
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_COLUMN);

    add_minimal_strip(tab, Metric::BoostRelBar,     "Pressione Turbo (Boost)", "bar", -0.5f, 2.5f, 2, ValueKind::Generic, lv_color_hex(mf::ACCENT_RED));
    add_minimal_strip(tab, Metric::BoostKpa,        "Collettore (MAP)",        "kPa", 20, 260,     0, ValueKind::PressureKpa, lv_color_hex(mf::ACCENT_AMBER));
    add_minimal_strip(tab, Metric::OilPressureBar,  "Pressione Olio",          "bar", 0.0f, 7.0f,  1, ValueKind::Generic, lv_color_hex(mf::ACCENT_CYAN));
    add_minimal_strip(tab, Metric::FuelPressureKpa, "Rail Carburante",         "kPa", 0, 16000,    0, ValueKind::PressureKpa, lv_color_hex(mf::ACCENT_GREEN));
}

void build_minimal_electric_tab(lv_obj_t* tab) {
    lv_obj_set_style_bg_opa(tab, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(tab, 8, 0);
    lv_obj_set_style_pad_row(tab, 8, 0);
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_COLUMN);

    add_minimal_strip(tab, Metric::BatteryVolts,    "Tensione Batteria",       "V", 9.0f, 16.0f,  2, ValueKind::Generic, lv_color_hex(mf::ACCENT_GREEN));
    add_minimal_strip(tab, Metric::Afr,             "Miscela Aria/Benzina",    "",  10.0f, 20.0f, 2, ValueKind::Generic, lv_color_hex(mf::ACCENT_CYAN));
    add_minimal_strip(tab, Metric::EngineLoad,      "Carico Motore",           "%", 0, 100,       0, ValueKind::Generic, lv_color_hex(mf::ACCENT_AMBER));
    add_minimal_strip(tab, Metric::Throttle,        "Apertura Farfalla",       "%", 0, 100,       0, ValueKind::Generic, lv_color_hex(mf::ACCENT_RED));
}

void build_minimal_fuel_tab(lv_obj_t* tab) {
    lv_obj_set_style_bg_opa(tab, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(tab, 8, 0);
    lv_obj_set_style_pad_row(tab, 8, 0);
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_COLUMN);

    add_minimal_strip(tab, Metric::FuelLevelPct,        "Livello Serbatoio",    "%",    0, 100,  0, ValueKind::Generic, lv_color_hex(mf::ACCENT_GREEN));
    add_minimal_strip(tab, Metric::FuelConsumptionL100, "Consumo Istantaneo",   "L/100",0, 30,   1, ValueKind::Generic, lv_color_hex(mf::ACCENT_AMBER));
    add_minimal_strip(tab, Metric::FuelRateLh,          "Portata Carburante",   "L/h",  0, 50,   2, ValueKind::Generic, lv_color_hex(mf::ACCENT_CYAN));
    add_minimal_strip(tab, Metric::DistanceSinceClear,  "Km da Reset DTC",      "km",   0, 1000, 0, ValueKind::Generic, lv_color_hex(mf::TEXT_MUTED));
}

void update_minimal_dashboard(int64_t now_ms) {
    if (!g_mf_dash.active) return;

    Sample s_rpm = abarth::data::store().get(Metric::Rpm);
    Sample s_spd = abarth::data::store().get(Metric::Speed);
    Sample s_ld  = abarth::data::store().get(Metric::EngineLoad);
    Sample s_th  = abarth::data::store().get(Metric::Throttle);
    Sample s_bst = abarth::data::store().get(Metric::BoostRelBar);
    Sample s_col = abarth::data::store().get(Metric::CoolantTempC);
    Sample s_oil = abarth::data::store().get(Metric::OilTempC);
    Sample s_bat = abarth::data::store().get(Metric::BatteryVolts);
    Sample s_afr = abarth::data::store().get(Metric::Afr);

    static const char* s_last_placeholder = nullptr;
    static int s_prev_rpm_i = -9999;
    static int s_prev_spd_i = -9999;
    static int s_prev_gear_ratio_bin = -9999;
    static int s_prev_ld_i = -9999;
    static int s_prev_th_i = -9999;
    static int s_prev_bst_i = -9999;
    static int s_prev_col_i = -9999;
    static int s_prev_col_badge = -9999;
    static int s_prev_oil_i = -9999;
    static int s_prev_oil_badge = -9999;
    static int s_prev_bat_i = -9999;
    static int s_prev_afr_i = -9999;
    static int s_prev_shift_light = -1;

    if (const char* p = connection_placeholder()) {
        if (p != s_last_placeholder) {
            s_last_placeholder = p;
            s_prev_rpm_i = -9999;
            s_prev_spd_i = -9999;
            s_prev_gear_ratio_bin = -9999;
            s_prev_ld_i = -9999;
            s_prev_th_i = -9999;
            s_prev_bst_i = -9999;
            s_prev_col_i = -9999;
            s_prev_col_badge = -9999;
            s_prev_oil_i = -9999;
            s_prev_oil_badge = -9999;
            s_prev_bat_i = -9999;
            s_prev_afr_i = -9999;
            s_prev_shift_light = -1;

            if (g_mf_dash.speed_label)      lv_label_set_text(g_mf_dash.speed_label, "--");
            if (g_mf_dash.rev_label)        lv_label_set_text(g_mf_dash.rev_label, "--- RPM");
            if (g_mf_dash.gear_label)       lv_label_set_text(g_mf_dash.gear_label, p);
            if (g_mf_dash.shift_light)      lv_obj_add_flag(g_mf_dash.shift_light, LV_OBJ_FLAG_HIDDEN);
            if (g_mf_dash.rev_bar)          lv_bar_set_value(g_mf_dash.rev_bar, 0, LV_ANIM_OFF);
            if (g_mf_dash.load_bar)         lv_bar_set_value(g_mf_dash.load_bar, 0, LV_ANIM_OFF);
            if (g_mf_dash.load_label)       lv_label_set_text(g_mf_dash.load_label, "--%");
            if (g_mf_dash.throttle_bar)     lv_bar_set_value(g_mf_dash.throttle_bar, 0, LV_ANIM_OFF);
            if (g_mf_dash.throttle_label)   lv_label_set_text(g_mf_dash.throttle_label, "--%");
            if (g_mf_dash.p1_boost_val)     lv_label_set_text(g_mf_dash.p1_boost_val, "-- bar");
            if (g_mf_dash.p1_boost_bar)     lv_bar_set_value(g_mf_dash.p1_boost_bar, 0, LV_ANIM_OFF);
            if (g_mf_dash.p2_coolant_val)   lv_label_set_text(g_mf_dash.p2_coolant_val, "--");
            if (g_mf_dash.p2_coolant_badge) {
                lv_label_set_text(g_mf_dash.p2_coolant_badge, "FREDDO");
                lv_obj_set_style_text_color(g_mf_dash.p2_coolant_badge, lv_color_hex(mf::TEXT_MUTED), 0);
            }
            if (g_mf_dash.p3_oil_val)       lv_label_set_text(g_mf_dash.p3_oil_val, "--");
            if (g_mf_dash.p3_oil_badge) {
                lv_label_set_text(g_mf_dash.p3_oil_badge, "STANDBY");
                lv_obj_set_style_text_color(g_mf_dash.p3_oil_badge, lv_color_hex(mf::TEXT_MUTED), 0);
            }
            if (g_mf_dash.p4_batt_val)      lv_label_set_text(g_mf_dash.p4_batt_val, "-- V");
            if (g_mf_dash.p4_afr_val)       lv_label_set_text(g_mf_dash.p4_afr_val, "AFR --");
        }
    } else {
        s_last_placeholder = nullptr;

        // RPM & Rev bar
        float rpm_val = 0;
        if (s_rpm.valid() && s_rpm.fresh(6000, now_ms)) {
            rpm_val = s_rpm.value;
            int rpm_i = static_cast<int>(rpm_val);
            if (rpm_i != s_prev_rpm_i) {
                s_prev_rpm_i = rpm_i;
                if (g_mf_dash.rev_bar) {
                    lv_bar_set_value(g_mf_dash.rev_bar, rpm_i, LV_ANIM_OFF);
                    lv_color_t bar_col = lv_color_hex(mf::ACCENT_GREEN);
                    if (rpm_i >= 5800)      bar_col = lv_color_hex(mf::ACCENT_RED);
                    else if (rpm_i >= 4600) bar_col = lv_color_hex(mf::ACCENT_AMBER);
                    lv_obj_set_style_bg_color(g_mf_dash.rev_bar, bar_col, LV_PART_INDICATOR);
                }
                if (g_mf_dash.rev_label) {
                    char b[24];
                    std::snprintf(b, sizeof(b), "%d RPM", rpm_i);
                    lv_label_set_text(g_mf_dash.rev_label, b);
                }
            }
            int shift_on = (rpm_i >= 5900) ? 1 : 0;
            if (shift_on != s_prev_shift_light) {
                s_prev_shift_light = shift_on;
                if (g_mf_dash.shift_light) {
                    if (shift_on) lv_obj_clear_flag(g_mf_dash.shift_light, LV_OBJ_FLAG_HIDDEN);
                    else          lv_obj_add_flag(g_mf_dash.shift_light, LV_OBJ_FLAG_HIDDEN);
                }
            }
        }

        // Speed & Gear
        float spd_val = 0;
        if (s_spd.valid() && s_spd.fresh(6000, now_ms)) {
            spd_val = settings::speedInCurrentUnit(s_spd.value);
            int spd_i = static_cast<int>(spd_val);
            if (spd_i != s_prev_spd_i) {
                s_prev_spd_i = spd_i;
                if (g_mf_dash.speed_label) {
                    char b[16];
                    std::snprintf(b, sizeof(b), "%d", spd_i);
                    lv_label_set_text(g_mf_dash.speed_label, b);
                }
            }

            // Calcolo marcia stimata per cambio C510 (5 marce Abarth)
            int gear_bin = 0;
            if (spd_val < 3.0f) {
                gear_bin = (rpm_val > 500) ? 10 : 0;
            } else if (rpm_val > 500) {
                float ratio = rpm_val / spd_val;
                if      (ratio > 68.0f) gear_bin = 1;
                else if (ratio > 38.0f) gear_bin = 2;
                else if (ratio > 26.0f) gear_bin = 3;
                else if (ratio > 20.0f) gear_bin = 4;
                else                    gear_bin = 5;
            }
            if (gear_bin != s_prev_gear_ratio_bin) {
                s_prev_gear_ratio_bin = gear_bin;
                if (g_mf_dash.gear_label) {
                    const char* g_str = "MARCIA -";
                    if (gear_bin == 0)       g_str = "STOP";
                    else if (gear_bin == 10) g_str = "FOLLE (N)";
                    else if (gear_bin == 1)  g_str = "MARCIA 1";
                    else if (gear_bin == 2)  g_str = "MARCIA 2";
                    else if (gear_bin == 3)  g_str = "MARCIA 3";
                    else if (gear_bin == 4)  g_str = "MARCIA 4";
                    else if (gear_bin == 5)  g_str = "MARCIA 5";
                    lv_label_set_text(g_mf_dash.gear_label, g_str);
                }
            }
        }

        // Carico & Throttle
        if (s_ld.valid() && s_ld.fresh(6000, now_ms)) {
            int ld = static_cast<int>(s_ld.value);
            if (ld != s_prev_ld_i) {
                s_prev_ld_i = ld;
                if (g_mf_dash.load_bar)   lv_bar_set_value(g_mf_dash.load_bar, ld, LV_ANIM_OFF);
                if (g_mf_dash.load_label) {
                    char b[16]; std::snprintf(b, sizeof(b), "%d%%", ld);
                    lv_label_set_text(g_mf_dash.load_label, b);
                }
            }
        }
        if (s_th.valid() && s_th.fresh(6000, now_ms)) {
            int th = static_cast<int>(s_th.value);
            if (th != s_prev_th_i) {
                s_prev_th_i = th;
                if (g_mf_dash.throttle_bar)   lv_bar_set_value(g_mf_dash.throttle_bar, th, LV_ANIM_OFF);
                if (g_mf_dash.throttle_label) {
                    char b[16]; std::snprintf(b, sizeof(b), "%d%%", th);
                    lv_label_set_text(g_mf_dash.throttle_label, b);
                }
            }
        }

        // Pillar 1: Boost
        if (s_bst.valid() && s_bst.fresh(6000, now_ms)) {
            int bv = static_cast<int>(s_bst.value * 100.0f);
            if (bv != s_prev_bst_i) {
                s_prev_bst_i = bv;
                if (g_mf_dash.p1_boost_val) {
                    char b[24]; std::snprintf(b, sizeof(b), "%.2f bar", s_bst.value);
                    lv_label_set_text(g_mf_dash.p1_boost_val, b);
                }
                if (g_mf_dash.p1_boost_bar) {
                    lv_bar_set_value(g_mf_dash.p1_boost_bar, (bv < 0 ? 0 : bv), LV_ANIM_OFF);
                }
            }
        }

        // Pillar 2: Coolant
        if (s_col.valid() && s_col.fresh(6000, now_ms)) {
            float col_v = settings::tempInCurrentUnit(s_col.value);
            int col_i = static_cast<int>(col_v);
            if (col_i != s_prev_col_i) {
                s_prev_col_i = col_i;
                if (g_mf_dash.p2_coolant_val) {
                    char b[24]; std::snprintf(b, sizeof(b), "%d %s", col_i, settings::tempUnitSymbol());
                    lv_label_set_text(g_mf_dash.p2_coolant_val, b);
                }
            }
            int col_b = (s_col.value < 70.0f) ? 0 : (s_col.value > 105.0f) ? 2 : 1;
            if (col_b != s_prev_col_badge) {
                s_prev_col_badge = col_b;
                if (g_mf_dash.p2_coolant_badge) {
                    if (col_b == 0) {
                        lv_label_set_text(g_mf_dash.p2_coolant_badge, "FREDDO");
                        lv_obj_set_style_text_color(g_mf_dash.p2_coolant_badge, lv_color_hex(mf::ACCENT_CYAN), 0);
                    } else if (col_b == 2) {
                        lv_label_set_text(g_mf_dash.p2_coolant_badge, "ALTA TEMP.");
                        lv_obj_set_style_text_color(g_mf_dash.p2_coolant_badge, lv_color_hex(mf::ACCENT_RED), 0);
                    } else {
                        lv_label_set_text(g_mf_dash.p2_coolant_badge, "OTTIMALE");
                        lv_obj_set_style_text_color(g_mf_dash.p2_coolant_badge, lv_color_hex(mf::ACCENT_GREEN), 0);
                    }
                }
            }
        }

        // Pillar 3: Oil Temp
        if (s_oil.valid() && s_oil.fresh(6000, now_ms)) {
            float oil_v = settings::tempInCurrentUnit(s_oil.value);
            int oil_i = static_cast<int>(oil_v);
            if (oil_i != s_prev_oil_i) {
                s_prev_oil_i = oil_i;
                if (g_mf_dash.p3_oil_val) {
                    char b[24]; std::snprintf(b, sizeof(b), "%d %s", oil_i, settings::tempUnitSymbol());
                    lv_label_set_text(g_mf_dash.p3_oil_val, b);
                }
            }
            int oil_b = (s_oil.value < 75.0f) ? 0 : (s_oil.value > 120.0f) ? 2 : 1;
            if (oil_b != s_prev_oil_badge) {
                s_prev_oil_badge = oil_b;
                if (g_mf_dash.p3_oil_badge) {
                    if (oil_b == 0) {
                        lv_label_set_text(g_mf_dash.p3_oil_badge, "SCALDANDO");
                        lv_obj_set_style_text_color(g_mf_dash.p3_oil_badge, lv_color_hex(mf::ACCENT_CYAN), 0);
                    } else if (oil_b == 2) {
                        lv_label_set_text(g_mf_dash.p3_oil_badge, "ALTA TEMP.");
                        lv_obj_set_style_text_color(g_mf_dash.p3_oil_badge, lv_color_hex(mf::ACCENT_RED), 0);
                    } else {
                        lv_label_set_text(g_mf_dash.p3_oil_badge, "A REGIME");
                        lv_obj_set_style_text_color(g_mf_dash.p3_oil_badge, lv_color_hex(mf::ACCENT_GREEN), 0);
                    }
                }
            }
        }

        // Pillar 4: Batt & AFR
        if (s_bat.valid() && s_bat.fresh(6000, now_ms) && g_mf_dash.p4_batt_val) {
            int bat_i = static_cast<int>(s_bat.value * 10.0f);
            if (bat_i != s_prev_bat_i) {
                s_prev_bat_i = bat_i;
                char b[24]; std::snprintf(b, sizeof(b), "%.1f V", s_bat.value);
                lv_label_set_text(g_mf_dash.p4_batt_val, b);
            }
        }
        if (s_afr.valid() && s_afr.fresh(6000, now_ms) && g_mf_dash.p4_afr_val) {
            int afr_i = static_cast<int>(s_afr.value * 100.0f);
            if (afr_i != s_prev_afr_i) {
                s_prev_afr_i = afr_i;
                char b[24]; std::snprintf(b, sizeof(b), "AFR %.2f", s_afr.value);
                lv_label_set_text(g_mf_dash.p4_afr_val, b);
            }
        }
    }

    // Telemetry strips update (Strisce Fisse & Scheda Personalizzata)
    auto update_one_strip = [&](MinimalStrip& st) {
        if (!st.bar || !st.val_label) return;
        Sample s = abarth::data::store().get(st.metric);
        char buf_g[32];
        bool real = format_value(st.kind, st.decimals, s, buf_g, sizeof(buf_g), now_ms);

        const char* display_str = real ? buf_g : (s.supported ? "--" : "N/D");

        if (std::strncmp(display_str, st.prev_text, sizeof(st.prev_text)) != 0) {
            std::snprintf(st.prev_text, sizeof(st.prev_text), "%s", display_str);
            lv_label_set_text(st.val_label, display_str);
        }
        if (real != !st.muted) {
            st.muted = !real;
            lv_obj_set_style_text_color(st.val_label,
                real ? lv_color_hex(mf::TEXT_WHITE) : lv_color_hex(mf::TEXT_MUTED), 0);
        }
        if (real && s.valid()) {
            float raw = s.value;
            if (st.kind == ValueKind::TempC)       raw = settings::tempInCurrentUnit(raw);
            if (st.kind == ValueKind::SpeedKmh)    raw = settings::speedInCurrentUnit(raw);
            if (st.kind == ValueKind::PressureKpa) raw = settings::pressureInCurrentUnit(raw);
            float ratio = (raw - st.min_val) / (st.max_val - st.min_val);
            if (ratio < 0.0f) ratio = 0.0f;
            if (ratio > 1.0f) ratio = 1.0f;
            int bar_v = static_cast<int>(ratio * 1000.0f);
            if (bar_v != st.last_bar_val) {
                st.last_bar_val = bar_v;
                lv_bar_set_value(st.bar, bar_v, LV_ANIM_OFF);
            }
        } else {
            if (st.last_bar_val != 0) {
                st.last_bar_val = 0;
                lv_bar_set_value(st.bar, 0, LV_ANIM_OFF);
            }
        }
        if (st.unit_label) {
            const char* sym = st.unit_str;
            if (st.kind == ValueKind::TempC)       sym = settings::tempUnitSymbol();
            if (st.kind == ValueKind::SpeedKmh)    sym = settings::speedUnitSymbol();
            if (st.kind == ValueKind::PressureKpa) sym = settings::pressureUnitSymbol();
            lv_label_set_text(st.unit_label, sym);
        }
    };

    for (size_t i = 0; i < g_mf_strip_count; ++i) {
        update_one_strip(g_mf_strips[i]);
    }
    for (size_t i = 0; i < g_custom_strip_count; ++i) {
        update_one_strip(g_custom_strips[i]);
    }
}

}  // namespace

void rebuild_custom_tab() {
    if (!g_t_custom) return;
    lv_obj_clean(g_t_custom);
    g_custom_strip_count = 0;

    lv_obj_set_style_bg_opa(g_t_custom, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(g_t_custom, 8, 0);
    lv_obj_set_style_pad_row(g_t_custom, 8, 0);
    lv_obj_set_flex_flow(g_t_custom, LV_FLEX_FLOW_COLUMN);

    uint8_t added = 0;
    for (uint8_t i = 0; i < static_cast<uint8_t>(Metric::Count_); ++i) {
        Metric m = static_cast<Metric>(i);
        if (!abarth::settings::isMetricInCustomTab(m)) continue;

        MetricMeta meta = get_metric_meta(m);
        add_custom_strip(g_t_custom, m, meta.title, meta.unit, meta.min_v, meta.max_v, meta.decimals, meta.kind, meta.color);
        added++;
    }

    if (added == 0) {
        lv_obj_t* empty_box = lv_obj_create(g_t_custom);
        lv_obj_remove_style_all(empty_box);
        lv_obj_set_size(empty_box, LV_PCT(100), 200);
        lv_obj_set_style_bg_color(empty_box, lv_color_hex(mf::SURFACE_DARK), 0);
        lv_obj_set_style_bg_opa(empty_box, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(empty_box, 12, 0);
        lv_obj_set_style_border_width(empty_box, 1, 0);
        lv_obj_set_style_border_color(empty_box, lv_color_hex(mf::SURFACE_BORDER), 0);
        lv_obj_set_flex_flow(empty_box, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(empty_box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_all(empty_box, 16, 0);
        lv_obj_set_style_pad_row(empty_box, 10, 0);

        lv_obj_t* icon = lv_label_create(empty_box);
        lv_label_set_text(icon, LV_SYMBOL_SETTINGS);
        lv_obj_set_style_text_font(icon, &lv_font_montserrat_28, 0);
        lv_obj_set_style_text_color(icon, lv_color_hex(mf::TEXT_DIM), 0);

        lv_obj_t* t = lv_label_create(empty_box);
        lv_label_set_text(t, "Pannello Personalizzato Vuoto");
        lv_obj_set_style_text_font(t, &lv_font_montserrat_18, 0);
        lv_obj_set_style_text_color(t, lv_color_hex(mf::TEXT_WHITE), 0);

        lv_obj_t* sub = lv_label_create(empty_box);
        lv_label_set_text(sub, "Vai in Impostazioni -> Personalizza Manometri\nper scegliere cosa visualizzare qui.");
        lv_obj_set_style_text_font(sub, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(sub, lv_color_hex(mf::TEXT_MUTED), 0);
        lv_obj_set_style_text_align(sub, LV_TEXT_ALIGN_CENTER, 0);
    }
}

void apply_metric_visibility() {
    // 1. Visibilità pulsante scheda custom sulla tab bar
    bool custom_en = abarth::settings::isCustomTabEnabled();
    if (g_tab_btn_custom) {
        if (custom_en) {
            lv_obj_clear_flag(g_tab_btn_custom, LV_OBJ_FLAG_HIDDEN);
        } else {
            if (g_main_tabview && lv_tabview_get_tab_active(g_main_tabview) == 5) {
                lv_tabview_set_act(g_main_tabview, 0, LV_ANIM_OFF);
            }
            lv_obj_add_flag(g_tab_btn_custom, LV_OBJ_FLAG_HIDDEN);
        }
    }

    // 2. Cockpit - RPM (rev_card)
    if (g_mf_dash.rev_card) {
        if (abarth::settings::isMetricVisible(Metric::Rpm)) {
            lv_obj_clear_flag(g_mf_dash.rev_card, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(g_mf_dash.rev_card, LV_OBJ_FLAG_HIDDEN);
        }
    }

    // 3. Cockpit - Middle Section (Speed & Dynamic Load/Throttle)
    const bool spd_vis = abarth::settings::isMetricVisible(Metric::Speed);
    const bool load_vis = abarth::settings::isMetricVisible(Metric::EngineLoad);
    const bool th_vis = abarth::settings::isMetricVisible(Metric::Throttle);
    const bool dyn_vis = (load_vis || th_vis);
    const bool is_portrait = (LV_HOR_RES < LV_VER_RES);

    if (g_mf_dash.spd_card) {
        if (spd_vis) {
            lv_obj_clear_flag(g_mf_dash.spd_card, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(g_mf_dash.spd_card, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (g_mf_dash.dyn_card) {
        if (dyn_vis) {
            lv_obj_clear_flag(g_mf_dash.dyn_card, LV_OBJ_FLAG_HIDDEN);
            if (g_mf_dash.r_ld) {
                if (load_vis) lv_obj_clear_flag(g_mf_dash.r_ld, LV_OBJ_FLAG_HIDDEN);
                else          lv_obj_add_flag(g_mf_dash.r_ld, LV_OBJ_FLAG_HIDDEN);
            }
            if (g_mf_dash.load_bar) {
                if (load_vis) lv_obj_clear_flag(g_mf_dash.load_bar, LV_OBJ_FLAG_HIDDEN);
                else          lv_obj_add_flag(g_mf_dash.load_bar, LV_OBJ_FLAG_HIDDEN);
            }
            if (g_mf_dash.r_th) {
                if (th_vis) lv_obj_clear_flag(g_mf_dash.r_th, LV_OBJ_FLAG_HIDDEN);
                else        lv_obj_add_flag(g_mf_dash.r_th, LV_OBJ_FLAG_HIDDEN);
            }
            if (g_mf_dash.throttle_bar) {
                if (th_vis) lv_obj_clear_flag(g_mf_dash.throttle_bar, LV_OBJ_FLAG_HIDDEN);
                else        lv_obj_add_flag(g_mf_dash.throttle_bar, LV_OBJ_FLAG_HIDDEN);
            }
        } else {
            lv_obj_add_flag(g_mf_dash.dyn_card, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (g_mf_dash.mid_row) {
        if (!spd_vis && !dyn_vis) {
            lv_obj_add_flag(g_mf_dash.mid_row, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_clear_flag(g_mf_dash.mid_row, LV_OBJ_FLAG_HIDDEN);
            if (is_portrait) {
                if (spd_vis && dyn_vis) {
                    lv_obj_set_size(g_mf_dash.spd_card, LV_PCT(100), 175);
                    lv_obj_set_size(g_mf_dash.dyn_card, LV_PCT(100), (load_vis && th_vis) ? 140 : 80);
                } else if (spd_vis && !dyn_vis) {
                    lv_obj_set_size(g_mf_dash.spd_card, LV_PCT(100), 220);
                } else if (!spd_vis && dyn_vis) {
                    lv_obj_set_size(g_mf_dash.dyn_card, LV_PCT(100), (load_vis && th_vis) ? 170 : 100);
                }
            } else {
                if (spd_vis && dyn_vis) {
                    lv_obj_set_size(g_mf_dash.spd_card, LV_PCT(62), LV_PCT(100));
                    lv_obj_set_size(g_mf_dash.dyn_card, LV_PCT(36), LV_PCT(100));
                } else if (spd_vis && !dyn_vis) {
                    lv_obj_set_size(g_mf_dash.spd_card, LV_PCT(100), LV_PCT(100));
                } else if (!spd_vis && dyn_vis) {
                    lv_obj_set_size(g_mf_dash.dyn_card, LV_PCT(100), LV_PCT(100));
                }
            }
        }
    }

    // 4. Cockpit - Bottom Section (4 Pillars)
    const bool p1_vis = abarth::settings::isMetricVisible(Metric::BoostRelBar) || abarth::settings::isMetricVisible(Metric::BoostKpa);
    const bool p2_vis = abarth::settings::isMetricVisible(Metric::CoolantTempC);
    const bool p3_vis = abarth::settings::isMetricVisible(Metric::OilTempC);
    const bool p4_vis = abarth::settings::isMetricVisible(Metric::BatteryVolts) || abarth::settings::isMetricVisible(Metric::Afr);

    if (g_mf_dash.p1) {
        if (p1_vis) lv_obj_clear_flag(g_mf_dash.p1, LV_OBJ_FLAG_HIDDEN);
        else        lv_obj_add_flag(g_mf_dash.p1, LV_OBJ_FLAG_HIDDEN);
    }
    if (g_mf_dash.p2) {
        if (p2_vis) lv_obj_clear_flag(g_mf_dash.p2, LV_OBJ_FLAG_HIDDEN);
        else        lv_obj_add_flag(g_mf_dash.p2, LV_OBJ_FLAG_HIDDEN);
    }
    if (g_mf_dash.p3) {
        if (p3_vis) lv_obj_clear_flag(g_mf_dash.p3, LV_OBJ_FLAG_HIDDEN);
        else        lv_obj_add_flag(g_mf_dash.p3, LV_OBJ_FLAG_HIDDEN);
    }
    if (g_mf_dash.p4) {
        if (p4_vis) lv_obj_clear_flag(g_mf_dash.p4, LV_OBJ_FLAG_HIDDEN);
        else        lv_obj_add_flag(g_mf_dash.p4, LV_OBJ_FLAG_HIDDEN);
    }

    int visible_pillars = (p1_vis ? 1 : 0) + (p2_vis ? 1 : 0) + (p3_vis ? 1 : 0) + (p4_vis ? 1 : 0);
    if (g_mf_dash.btm_row) {
        if (visible_pillars == 0) {
            lv_obj_add_flag(g_mf_dash.btm_row, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_clear_flag(g_mf_dash.btm_row, LV_OBJ_FLAG_HIDDEN);
            if (is_portrait) {
                lv_obj_t* p_arr[4] = { g_mf_dash.p1, g_mf_dash.p2, g_mf_dash.p3, g_mf_dash.p4 };
                bool vis_arr[4] = { p1_vis, p2_vis, p3_vis, p4_vis };
                for (int i = 0; i < 4; ++i) {
                    if (p_arr[i] && vis_arr[i]) {
                        if (visible_pillars == 1) {
                            lv_obj_set_width(p_arr[i], LV_PCT(100));
                        } else {
                            lv_obj_set_width(p_arr[i], LV_PCT(48));
                            lv_obj_set_flex_grow(p_arr[i], 1);
                        }
                    }
                }
            }
        }
    }

    // 5. Strisce telemetriche fisse (Tabs 1, 2, 3, 4)
    for (size_t i = 0; i < g_mf_strip_count; ++i) {
        MinimalStrip& st = g_mf_strips[i];
        if (!st.row) continue;
        if (abarth::settings::isMetricVisible(st.metric)) {
            lv_obj_clear_flag(st.row, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(st.row, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void build() {
    // LVGL gira in un task dedicato (port v8): ogni accesso alle API LVGL va
    // fatto con il mutex preso.
    if (!lvgl_port_lock(-1)) {
        ESP_LOGE(TAG, "lvgl_port_lock fallito");
        return;
    }

    ensure_styles();
    g_mf_dash = {};
    g_mf_strip_count = 0;

    // Lo splash utilizza uno screen dedicato; qui creiamo il nuovo screen
    // principale e carichiamolo. `splash::dismiss()` si occupa di cancellare
    // quello precedente.
    lv_obj_t* scr = lv_obj_create(nullptr);
    lv_obj_remove_style_all(scr);
    lv_obj_set_size(scr, LV_HOR_RES, LV_VER_RES);

    const auto bg = settings::current().bg_texture;
    lv_obj_set_style_bg_color(scr, lv_color_hex(mf::BG_DEEP), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    if (bg == settings::BgTexture::CarbonImage) {
        // --- Procedural Carbon Fiber 2x2 Twill Weave (full-screen canvas) ---
        const int W = LV_HOR_RES;
        const int H = LV_VER_RES;
        uint16_t* cbuf = (uint16_t*)heap_caps_malloc(
            W * H * sizeof(uint16_t), MALLOC_CAP_SPIRAM);

        if (cbuf) {
            // Strand width in pixels (each fiber bundle)
            const int S = 6;
            // Full weave period = 4 strands (2 over + 2 under per direction)
            const int P = S * 4;

            // Rounded strand brightness profile (simulates cylindrical fiber
            // bundle cross-section: dark gap edges -> bright center highlight)
            //                        edge  side  body  peak  body  edge
            static const uint8_t profile[6] = {4, 14, 22, 28, 20, 6};

            for (int y = 0; y < H; y++) {
                for (int x = 0; x < W; x++) {
                    // Position within each diagonal fiber direction
                    // d1 = fibers going top-left -> bottom-right (+45°)
                    // d2 = fibers going top-right -> bottom-left (-45°)
                    int d1 = ((x + y) % P + P) % P;
                    int d2 = ((x - y) % P + P) % P;

                    // Which strand index (0..3) for each direction
                    int s1 = d1 / S;
                    int s2 = d2 / S;

                    // 2x2 twill: fiber set 1 is on top when (s1+s2) is even
                    bool f1_top = ((s1 + s2) % 2) == 0;

                    // Position within the visible (top) strand
                    int pos = f1_top ? (d1 % S) : (d2 % S);
                    uint8_t bright = profile[pos];

                    // Darken at weave transition boundaries (gap between bundles)
                    int edge1 = d1 % S;
                    int edge2 = d2 % S;
                    if (edge1 == 0 || edge2 == 0) {
                        bright = bright > 6 ? bright - 6 : 0;
                    }

                    // Subtle specular highlight near weave crossings
                    bool near_cross1 = (d1 % (S * 2)) >= S - 1 && (d1 % (S * 2)) <= S + 1;
                    bool near_cross2 = (d2 % (S * 2)) >= S - 1 && (d2 % (S * 2)) <= S + 1;
                    if (near_cross1 && near_cross2 && bright > 16) {
                        bright += 10;
                        if (bright > 44) bright = 44;
                    }

                    cbuf[y * W + x] = lv_color_to_u16(lv_color_make(bright, bright, bright));
                }
            }

            lv_obj_t* canvas = lv_canvas_create(scr);
            lv_canvas_set_buffer(canvas, cbuf, W, H, LV_COLOR_FORMAT_RGB565);
            lv_obj_set_pos(canvas, 0, 0);
            lv_obj_add_flag(canvas, LV_OBJ_FLAG_FLOATING);
            lv_obj_move_to_index(canvas, 0);
            // NB: cbuf is intentionally never freed – it backs the canvas
            // for the lifetime of the screen.
        }
    } else if (bg == settings::BgTexture::CheckeredFlag) {
        // --- Bandiera a scacchi racing (top half) + nero (bottom half) ---
        const int W = LV_HOR_RES;
        const int H = LV_VER_RES;
        uint16_t* cbuf = (uint16_t*)heap_caps_malloc(
            W * H * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
        if (cbuf) {
            const int sq = 48;             // lato cella
            const int half = H / 2;        // meta' superiore a scacchi
            for (int y = 0; y < H; y++) {
                for (int x = 0; x < W; x++) {
                    lv_color_t c;
                    if (y < half) {
                        int cx = x / sq;
                        int cy = y / sq;
                        bool white = ((cx + cy) % 2) == 0;
                        if (white) {
                            // leggera sfumatura verticale verso il basso
                            uint8_t v = 235 - (uint8_t)((y * 25) / half);
                            c = lv_color_make(v, v, v);
                        } else {
                            c = lv_color_make(8, 8, 10);
                        }
                    } else {
                        // sfondo nero profondo con leggera gradazione
                        uint8_t v = 6 + (uint8_t)(((y - half) * 8) / (H - half));
                        c = lv_color_make(v, v, v + 2);
                    }
                    cbuf[y * W + x] = lv_color_to_u16(c);
                }
            }
            lv_obj_t* canvas = lv_canvas_create(scr);
            lv_canvas_set_buffer(canvas, cbuf, W, H, LV_COLOR_FORMAT_RGB565);
            lv_obj_set_pos(canvas, 0, 0);
            lv_obj_add_flag(canvas, LV_OBJ_FLAG_FLOATING);
            lv_obj_move_to_index(canvas, 0);
        }
    } else if (bg == settings::BgTexture::ItalianFlag) {
        // --- Tricolore italiano sfumato soft (verde-bianco-rosso in trasparenza) ---
        const int W = LV_HOR_RES;
        const int H = LV_VER_RES;
        uint16_t* cbuf = (uint16_t*)heap_caps_malloc(
            W * H * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
        if (cbuf) {
            // Tre fasce verticali con sfumatura orizzontale molto soft.
            // Bandiera reale: 1/3 verde, 1/3 bianco, 1/3 rosso.
            // Qui: colori scuri per restare leggibili sui numeri bianchi.
            for (int y = 0; y < H; y++) {
                for (int x = 0; x < W; x++) {
                    float fx = (float)x / (float)W;  // 0..1
                    uint8_t r = 0, g = 0, b = 0;
                    if (fx < 0.34f) {
                        // Verde scuro Abarth (#0a3d1f)
                        g = 18 + (uint8_t)(8.0f * (1.0f - fabsf(fx - 0.17f) * 3.0f));
                        r = 6; b = 14;
                    } else if (fx < 0.66f) {
                        // Bianco/grigio soft
                        uint8_t v = 22 + (uint8_t)(6.0f * (1.0f - fabsf(fx - 0.5f) * 3.0f));
                        r = v; g = v; b = v;
                    } else {
                        // Rosso scuro Abarth (#5a0e0e)
                        r = 60 + (uint8_t)(15.0f * (1.0f - fabsf(fx - 0.83f) * 3.0f));
                        g = 8; b = 10;
                    }
                    // Fondo: la luminosita' scende verso i bordi per dare profondita'
                    float vy = (float)y / (float)H;  // 0..1
                    float vignette = 1.0f - 0.35f * fabsf(vy - 0.5f) * 2.0f;
                    r = (uint8_t)(r * vignette);
                    g = (uint8_t)(g * vignette);
                    b = (uint8_t)(b * vignette);
                    cbuf[y * W + x] = lv_color_to_u16(lv_color_make(r, g, b));
                }
            }
            lv_obj_t* canvas = lv_canvas_create(scr);
            lv_canvas_set_buffer(canvas, cbuf, W, H, LV_COLOR_FORMAT_RGB565);
            lv_obj_set_pos(canvas, 0, 0);
            lv_obj_add_flag(canvas, LV_OBJ_FLAG_FLOATING);
            lv_obj_move_to_index(canvas, 0);
        }
    } else if (bg == settings::BgTexture::ScorpionWatermark) {
        // --- Scorpione Abarth watermark: disegno vettoriale stilizzato semi-trasparente ---
        const int W = LV_HOR_RES;
        const int H = LV_VER_RES;
        uint16_t* cbuf = (uint16_t*)heap_caps_malloc(
            W * H * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
        if (cbuf) {
            // Sfondo carbonio scuro uniforme
            uint16_t bg_col = lv_color_to_u16(lv_color_make(10, 10, 12));
            for (int i = 0; i < W * H; ++i) {
                cbuf[i] = bg_col;
            }
            // Disegna scorpione stilizzato al centro (corpo + coda + zampe + chele)
            // usando primitive geometriche. Coordinate relative al centro.
            const float cx = W * 0.5f;
            const float cy = H * 0.5f;
            const float scale = (W < H ? W : H) * 0.0018f;  // scala rispetto a 1024px
            const uint16_t sc = lv_color_to_u16(lv_color_make(80, 14, 14));  // rosso scorpione semi-trasp
            // Helper: disegna un disco (cerchio pieno)
            auto drawDisc = [&](float px, float py, float r) {
                int ir = (int)(r + 1);
                for (int dy = -ir; dy <= ir; ++dy) {
                    for (int dx = -ir; dx <= ir; ++dx) {
                        if (dx * dx + dy * dy <= (int)(r * r)) {
                            int fx = (int)(px + dx);
                            int fy = (int)(py + dy);
                            if (fx >= 0 && fx < W && fy >= 0 && fy < H) {
                                cbuf[fy * W + fx] = sc;
                            }
                        }
                    }
                }
            };
            // Helper: disegna una linea spessa tra due punti (per zampe/coda)
            auto drawSeg = [&](float x0, float y0, float x1, float y1, float w) {
                int steps = (int)(sqrtf((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0)) + 1);
                for (int s = 0; s <= steps; ++s) {
                    float t = (float)s / steps;
                    drawSeg_helper(cbuf, W, H, x0 + (x1 - x0) * t, y0 + (y1 - y0) * t, w, sc);
                }
            };
            (void)drawSeg;  // (i singoli segmenti vanno aggiunti sotto)
            // Corpo: segmenti del corpo (testa->torace->addome)
            const float U = scale * 50.0f;  // unita'
            drawDisc(cx, cy - 4 * U, 1.0f * U);                // testa
            drawDisc(cx, cy - 2 * U, 1.4f * U);                // torace
            drawDisc(cx, cy + 1 * U, 1.8f * U);                // addome
            drawDisc(cx, cy + 4 * U, 1.2f * U);                // coda base
            // Coda: curva ad S verso l'alto con pallina finale (pungiglione)
            float px = cx, py = cy + 4 * U;
            const float tailPts[][2] = {
                {cx + 1.0f * U, cy + 2.5f * U},
                {cx + 2.5f * U, cy + 0.5f * U},
                {cx + 2.5f * U, cy - 2.0f * U},
                {cx + 1.0f * U, cy - 3.5f * U},
                {cx - 0.5f * U, cy - 4.5f * U},
            };
            for (const auto& p : tailPts) {
                drawSeg_helper2(cbuf, W, H, px, py, p[0], p[1], 0.6f * U, sc);
                drawDisc(p[0], p[1], 0.6f * U);
                px = p[0]; py = p[1];
            }
            drawDisc(px, py, 0.9f * U);  // pungiglione
            // Zampe sinistra e destra (3 per lato)
            for (int side = -1; side <= 1; side += 2) {
                for (int z = 0; z < 3; ++z) {
                    float base_y = cy - 2.5f * U + z * 1.4f * U;
                    drawSeg_helper2(cbuf, W, H,
                        cx + 1.0f * U * side, base_y,
                        cx + 4.5f * U * side, base_y + 1.2f * U,
                        0.4f * U, sc);
                }
            }
            // Chele (pinze) anteriori
            for (int side = -1; side <= 1; side += 2) {
                drawSeg_helper2(cbuf, W, H,
                    cx + 1.0f * U * side, cy - 3.5f * U,
                    cx + 3.5f * U * side, cy - 4.5f * U,
                    0.6f * U, sc);
                drawDisc(cx + 3.5f * U * side, cy - 4.5f * U, 1.0f * U);
            }
            lv_obj_t* canvas = lv_canvas_create(scr);
            lv_canvas_set_buffer(canvas, cbuf, W, H, LV_COLOR_FORMAT_RGB565);
            lv_obj_set_pos(canvas, 0, 0);
            lv_obj_add_flag(canvas, LV_OBJ_FLAG_FLOATING);
            lv_obj_move_to_index(canvas, 0);
        }
    } else if (bg == settings::BgTexture::AsphaltTrack) {
        // --- Texture asfalto da circuito: rumore grigio scuro + segnaletica ---
        const int W = LV_HOR_RES;
        const int H = LV_VER_RES;
        uint16_t* cbuf = (uint16_t*)heap_caps_malloc(
            W * H * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
        if (cbuf) {
            // PRNG deterministico (xorshift) per riproducibilita'
            uint32_t rng = 0x1234ABCD;
            auto nextRand = [&rng]() {
                rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
                return rng;
            };
            // Base asfalto: grigio molto scuro con rumore granulare
            for (int y = 0; y < H; y++) {
                for (int x = 0; x < W; x++) {
                    uint32_t n = nextRand() & 0xFF;
                    uint8_t base = 18;
                    // aggregato piu' chiaro sparse
                    if ((n & 0x3F) > 0x38) base = 32 + (n & 0x1F);
                    else if ((n & 0x1F) > 0x18) base = 22 + (n & 0x07);
                    else base = 14 + (n & 0x05);
                    // vignetting verticale
                    float vy = (float)y / (float)H;
                    float vig = 1.0f - 0.45f * fabsf(vy - 0.5f) * 2.0f;
                    uint8_t v = (uint8_t)(base * vig);
                    cbuf[y * W + x] = lv_color_to_u16(lv_color_make(v, v, v));
                }
            }
            // Strisce di segnaletica (bianche e rosse ai bordi, tipo kerb)
            const int kerb_w = 12;
            for (int y = 0; y < H; y++) {
                int seg = (y / 32) % 2;
                uint16_t kc = lv_color_to_u16(seg ? lv_color_make(210, 210, 210) : lv_color_make(180, 20, 20));
                for (int x = 0; x < kerb_w; x++) {
                    cbuf[y * W + x] = kc;
                    cbuf[y * W + (W - 1 - x)] = kc;
                }
            }
            lv_obj_t* canvas = lv_canvas_create(scr);
            lv_canvas_set_buffer(canvas, cbuf, W, H, LV_COLOR_FORMAT_RGB565);
            lv_obj_set_pos(canvas, 0, 0);
            lv_obj_add_flag(canvas, LV_OBJ_FLAG_FLOATING);
            lv_obj_move_to_index(canvas, 0);
        }
    }

    const bool is_portrait_mode = (LV_HOR_RES < LV_VER_RES);

    /* --- Header ----------------------------------------------------------- */
    const int header_h = 48;
    lv_obj_t* header = lv_obj_create(scr);
    lv_obj_remove_style_all(header);
    lv_obj_set_size(header, LV_PCT(100), header_h);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(mf::BG_DEEP), 0);
    lv_obj_set_style_bg_opa(header, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(header, 12, 0);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    /* 1. Left Box: Logo Scudetto Abarth + Brand */
    lv_obj_t* left_box = lv_obj_create(header);
    lv_obj_remove_style_all(left_box);
    lv_obj_set_size(left_box, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(left_box, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(left_box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(left_box, 6, 0);

    lv_obj_t* ico_scorp = lv_image_create(left_box);
    if (abarth::storage::sd().isMounted()) {
        lv_image_set_src(ico_scorp, "S:/icons/abarth_logo.png");
    } else {
        lv_image_set_src(ico_scorp, &g_abarth_logo_img_dsc);
    }

    lv_obj_t* title = lv_label_create(left_box);
    lv_obj_set_style_text_color(title, lv_color_hex(mf::TEXT_WHITE), 0);
    lv_obj_set_style_text_font(title, is_portrait_mode ? &lv_font_montserrat_16 : &lv_font_montserrat_14, 0);
    lv_label_set_recolor(title, true);
    lv_label_set_text(title, is_portrait_mode ? "#EF4444 ABARTH#" : "#EF4444 ABARTH# #FFFFFF TELEMETRY#");

    /* 2. Center Box: Warmup & Countdown Badge */
    g_warmup_badge = lv_obj_create(header);
    lv_obj_remove_style_all(g_warmup_badge);
    lv_obj_set_size(g_warmup_badge, LV_SIZE_CONTENT, 28);
    lv_obj_set_style_bg_color(g_warmup_badge, lv_color_hex(0x0C1929), 0);
    lv_obj_set_style_bg_opa(g_warmup_badge, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(g_warmup_badge, lv_color_hex(0x0284C7), 0);
    lv_obj_set_style_border_width(g_warmup_badge, 1, 0);
    lv_obj_set_style_radius(g_warmup_badge, 14, 0);
    lv_obj_set_style_pad_hor(g_warmup_badge, 10, 0);
    lv_obj_set_flex_flow(g_warmup_badge, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(g_warmup_badge, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(g_warmup_badge, 6, 0);

    g_warmup_icon = lv_image_create(g_warmup_badge);
    if (abarth::storage::sd().isMounted()) {
        lv_image_set_src(g_warmup_icon, "S:/icons/oil_blue.png");
    } else {
        lv_image_set_src(g_warmup_icon, &g_oil_blue_img_dsc);
    }
    lv_image_set_scale(g_warmup_icon, 80);
    lv_obj_set_size(g_warmup_icon, 20, 20);
    lv_image_set_inner_align(g_warmup_icon, LV_IMAGE_ALIGN_CENTER);
    lv_obj_set_style_image_recolor_opa(g_warmup_icon, LV_OPA_TRANSP, 0);

    g_warmup_lbl = lv_label_create(g_warmup_badge);
    lv_obj_set_style_text_font(g_warmup_lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(g_warmup_lbl, lv_color_hex(0x38BDF8), 0);
    lv_label_set_text(g_warmup_lbl, "MOTORE FREDDO");

    /* 3. Right Box: MicroSD + Orologio + Badge Connessione */
    lv_obj_t* right = lv_obj_create(header);
    lv_obj_remove_style_all(right);
    lv_obj_set_size(right, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(right, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(right, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(right, 8, 0);

    g_sd_label = lv_label_create(right);
    lv_obj_set_style_text_color(g_sd_label, lv_color_hex(0x10B981), 0);
    lv_obj_set_style_text_font(g_sd_label, &lv_font_montserrat_14, 0);
    if (abarth::storage::sd().isMounted()) {
        lv_label_set_text(g_sd_label, LV_SYMBOL_SD_CARD);
    } else {
        lv_label_set_text(g_sd_label, "");
    }

    g_time_label = lv_label_create(right);
    lv_obj_set_style_text_color(g_time_label, lv_color_hex(0xF3F4F6), 0);
    lv_obj_set_style_text_font(g_time_label, &lv_font_montserrat_14, 0);
    lv_label_set_text(g_time_label, "");

    g_conn_badge = lv_obj_create(right);
    lv_obj_remove_style_all(g_conn_badge);
    lv_obj_set_size(g_conn_badge, LV_SIZE_CONTENT, 26);
    lv_obj_set_style_bg_color(g_conn_badge, lv_color_hex(0x141416), 0);
    lv_obj_set_style_bg_opa(g_conn_badge, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(g_conn_badge, lv_color_hex(0x3F3F46), 0);
    lv_obj_set_style_border_width(g_conn_badge, 1, 0);
    lv_obj_set_style_radius(g_conn_badge, 13, 0);
    lv_obj_set_style_pad_hor(g_conn_badge, 8, 0);
    lv_obj_set_flex_flow(g_conn_badge, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(g_conn_badge, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(g_conn_badge, 4, 0);

    g_conn_icon = lv_label_create(g_conn_badge);
    lv_obj_set_style_text_font(g_conn_icon, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(g_conn_icon, lv_color_hex(0x71717A), 0);
    lv_label_set_text(g_conn_icon, LV_SYMBOL_BLUETOOTH);

    g_conn_label = lv_label_create(g_conn_badge);
    lv_obj_set_style_text_font(g_conn_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(g_conn_label, lv_color_hex(0x71717A), 0);
    lv_label_set_text(g_conn_label, "OFFLINE");
    g_state_label = g_conn_label;

    /* --- Tabview ---------------------------------------------------------- */
    const int tab_bar_h = 44;
    lv_obj_t* tabview = lv_tabview_create(scr);
    g_main_tabview = tabview;
    lv_tabview_set_tab_bar_position(tabview, LV_DIR_TOP);
    lv_tabview_set_tab_bar_size(tabview, tab_bar_h);
    lv_obj_set_size(tabview, LV_PCT(100), LV_VER_RES - header_h);
    lv_obj_align(tabview, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(tabview, LV_OPA_TRANSP, 0);

    lv_obj_add_flag(g_conn_badge, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(g_conn_badge, [](lv_event_t* e) {
        lv_obj_t* tv = (lv_obj_t*)lv_event_get_user_data(e);
        if (tv) lv_tabview_set_act(tv, 6, LV_ANIM_OFF);
    }, LV_EVENT_CLICKED, tabview);

    lv_obj_add_flag(left_box, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(left_box, [](lv_event_t* e) {
        lv_obj_t* tv = (lv_obj_t*)lv_event_get_user_data(e);
        if (tv) lv_tabview_set_act(tv, 0, LV_ANIM_OFF);
    }, LV_EVENT_CLICKED, tabview);
    lv_obj_t* tv_content = lv_tabview_get_content(tabview);
    if (tv_content) {
        lv_obj_set_style_bg_opa(tv_content, LV_OPA_TRANSP, 0);
        lv_obj_clear_flag(tv_content, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(tv_content, LV_OBJ_FLAG_SCROLL_CHAIN);
        lv_obj_set_scroll_dir(tv_content, LV_DIR_NONE);
    }

    lv_obj_t* t_motor  = lv_tabview_add_tab(tabview, is_portrait_mode ? LV_SYMBOL_POWER " Motore" : LV_SYMBOL_POWER " Motore");
    lv_obj_t* t_temp   = lv_tabview_add_tab(tabview, is_portrait_mode ? LV_SYMBOL_REFRESH " Temp" : LV_SYMBOL_REFRESH " Temperature");
    lv_obj_t* t_pres   = lv_tabview_add_tab(tabview, is_portrait_mode ? LV_SYMBOL_DRIVE " Press" : LV_SYMBOL_DRIVE " Pressioni");
    lv_obj_t* t_elec   = lv_tabview_add_tab(tabview, is_portrait_mode ? LV_SYMBOL_CHARGE " Volt" : LV_SYMBOL_CHARGE " Elettrico");
    lv_obj_t* t_fuel   = lv_tabview_add_tab(tabview, is_portrait_mode ? LV_SYMBOL_TINT " Fuel" : LV_SYMBOL_BATTERY_FULL " Carburante");
    lv_obj_t* t_custom = lv_tabview_add_tab(tabview, is_portrait_mode ? LV_SYMBOL_SHUFFLE " Custom" : LV_SYMBOL_SHUFFLE " Personalizzato");
    lv_obj_t* t_set    = lv_tabview_add_tab(tabview, is_portrait_mode ? LV_SYMBOL_SETTINGS " Setup" : LV_SYMBOL_SETTINGS " Impostazioni");

    g_t_custom = t_custom;

    lv_obj_t* tab_bar = lv_tabview_get_tab_bar(tabview);
    if (tab_bar) {
        lv_obj_set_style_bg_color(tab_bar, lv_color_hex(mf::BG_DEEP), 0);
        lv_obj_set_style_bg_opa(tab_bar, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_all(tab_bar, 0, 0);
        lv_obj_set_style_pad_column(tab_bar, 1, 0);
        lv_obj_set_style_border_color(tab_bar, lv_color_hex(0x27272A), 0);
        lv_obj_set_style_border_width(tab_bar, 1, 0);
        lv_obj_set_style_border_side(tab_bar, LV_BORDER_SIDE_BOTTOM, 0);

        uint32_t btn_cnt = lv_obj_get_child_count(tab_bar);
        for (uint32_t i = 0; i < btn_cnt; i++) {
            lv_obj_t* btn = lv_obj_get_child(tab_bar, i);
            if (!btn) continue;
            lv_obj_set_flex_grow(btn, 1);
            lv_obj_set_style_pad_hor(btn, 1, 0);
            lv_obj_set_style_pad_ver(btn, 2, 0);
            lv_obj_set_style_radius(btn, 0, 0);
            lv_obj_set_style_bg_color(btn, lv_color_hex(mf::BG_DEEP), 0);
            lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
            lv_obj_set_style_bg_color(btn, lv_color_hex(mf::SURFACE_DARK), LV_STATE_CHECKED);
            lv_obj_set_style_border_color(btn, lv_color_hex(mf::ACCENT_RED), LV_STATE_CHECKED);
            lv_obj_set_style_border_width(btn, 3, LV_STATE_CHECKED);
            lv_obj_set_style_border_side(btn, LV_BORDER_SIDE_BOTTOM, LV_STATE_CHECKED);

            lv_obj_t* lbl = lv_obj_get_child(btn, 0);
            if (lbl) {
                lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
                lv_obj_set_style_text_color(lbl, lv_color_hex(mf::TEXT_MUTED), 0);
                lv_obj_set_style_text_color(lbl, lv_color_hex(mf::TEXT_WHITE), LV_STATE_CHECKED);
            }
        }
        g_tab_btn_custom = lv_obj_get_child(tab_bar, 5);
    }

    /* === MINIMAL FUNZIONALE: Cockpit Essenziale e Strisce Telemetriche === */
    build_minimal_cockpit_tab(t_motor);
    build_minimal_temp_tab(t_temp);
    build_minimal_pressure_tab(t_pres);
    build_minimal_electric_tab(t_elec);
    build_minimal_fuel_tab(t_fuel);
    rebuild_custom_tab();

    /* Impostazioni --------------------------------------------------------- */
    settings_tab::build(t_set);
    scroll_bench::start(tabview, 6, settings_tab::scroll_target());

    /* Applica visibilità iniziale configurata ------------------------------ */
    apply_metric_visibility();

    /* Timer periodico UI --------------------------------------------------- */
#ifndef ABARTH_DIAG_NO_UI_TIMER
    lv_timer_create(on_tick, abarth::config::kUiUpdateMs, nullptr);
#else
    printf("[I][ui] [DIAG] on_tick timer NON creato (ABARTH_DIAG_NO_UI_TIMER)\n");
    fflush(stdout);
#endif

    lv_scr_load(scr);
    lvgl_port_unlock();
}

}  // namespace abarth::ui

