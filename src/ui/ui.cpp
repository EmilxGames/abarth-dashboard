#include "ui/ui.h"

#include <lvgl.h>
#include <esp_log.h>

#include <array>
#include <cmath>
#include <cstdio>

#include "config/app_config.h"
#include "data/data_store.h"
#include "display/lvgl_v8_port.h"
#include "obd/obd_manager.h"

namespace abarth::ui {

namespace {
constexpr const char* TAG = "ui";

using abarth::data::Metric;
using abarth::data::Sample;

struct Card {
    Metric      metric;
    const char* title;
    const char* unit;
    int         decimals;
    lv_obj_t*   value_label;
    lv_obj_t*   title_label;
};

std::array<Card, 32> g_cards;
size_t               g_card_count = 0;

lv_obj_t* g_state_label    = nullptr;
lv_obj_t* g_counters_label = nullptr;

/* --- stili ---------------------------------------------------------------- */

lv_style_t style_card;
lv_style_t style_title;
lv_style_t style_value;
lv_style_t style_unit;
bool       styles_ready = false;

void ensure_styles() {
    if (styles_ready) return;
    styles_ready = true;

    lv_style_init(&style_card);
    lv_style_set_radius(&style_card, 12);
    lv_style_set_bg_opa(&style_card, LV_OPA_COVER);
    lv_style_set_bg_color(&style_card, lv_color_hex(0x1E1E24));
    lv_style_set_border_width(&style_card, 1);
    lv_style_set_border_color(&style_card, lv_color_hex(0x33333A));
    lv_style_set_pad_all(&style_card, 10);

    lv_style_init(&style_title);
    lv_style_set_text_color(&style_title, lv_color_hex(0x9A9AA6));
    lv_style_set_text_font(&style_title, &lv_font_montserrat_16);

    lv_style_init(&style_value);
    lv_style_set_text_color(&style_value, lv_color_hex(0xF8F8F8));
    lv_style_set_text_font(&style_value, &lv_font_montserrat_48);

    lv_style_init(&style_unit);
    lv_style_set_text_color(&style_unit, lv_color_hex(0xCC3030));
    lv_style_set_text_font(&style_unit, &lv_font_montserrat_20);
}

Card& add_card(lv_obj_t* parent, Metric m, const char* title, const char* unit, int decimals = 1) {
    if (g_card_count >= g_cards.size()) {
        ESP_LOGE(TAG, "troppe card, aumenta g_cards");
        static Card dummy{};
        return dummy;
    }
    Card& c    = g_cards[g_card_count++];
    c.metric   = m;
    c.title    = title;
    c.unit     = unit;
    c.decimals = decimals;

    lv_obj_t* cont = lv_obj_create(parent);
    lv_obj_remove_style_all(cont);
    lv_obj_add_style(cont, &style_card, 0);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    c.title_label = lv_label_create(cont);
    lv_obj_add_style(c.title_label, &style_title, 0);
    lv_label_set_text(c.title_label, title);

    c.value_label = lv_label_create(cont);
    lv_obj_add_style(c.value_label, &style_value, 0);
    lv_label_set_text(c.value_label, "—");

    lv_obj_t* u = lv_label_create(cont);
    lv_obj_add_style(u, &style_unit, 0);
    lv_label_set_text(u, unit);
    return c;
}

void set_card_size(Card& c, int cols, int rows) {
    if (!c.value_label) return;
    lv_obj_t* cont  = lv_obj_get_parent(c.value_label);
    const int pct_w = 100 / cols - 2;
    const int pct_h = 100 / rows - 2;
    lv_obj_set_size(cont, LV_PCT(pct_w), LV_PCT(pct_h));
}

void setup_grid_tab(lv_obj_t* tab) {
    lv_obj_set_style_pad_all(tab, 12, 0);
    lv_obj_set_style_pad_row(tab, 10, 0);
    lv_obj_set_style_pad_column(tab, 10, 0);
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(tab, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
}

void format_value(const Card& c, const Sample& s, char* out, size_t out_sz, int64_t now_ms) {
    if (!s.fresh(3000, now_ms)) { std::snprintf(out, out_sz, "—"); return; }
    if (std::isnan(s.value))    { std::snprintf(out, out_sz, "—"); return; }
    if (c.decimals <= 0) std::snprintf(out, out_sz, "%.0f", s.value);
    else                 std::snprintf(out, out_sz, "%.*f", c.decimals, s.value);
}

void on_tick(lv_timer_t* /*t*/) {
    const int64_t now_ms = esp_timer_get_time() / 1000;
    char buf[32];
    for (size_t i = 0; i < g_card_count; ++i) {
        Card& c = g_cards[i];
        if (!c.value_label) continue;
        Sample s = abarth::data::store().get(c.metric);
        format_value(c, s, buf, sizeof(buf), now_ms);
        lv_label_set_text(c.value_label, buf);
    }

    if (g_state_label) {
        lv_label_set_text(g_state_label,
                          abarth::obd::stateName(abarth::obd::obd().state()));
    }
    if (g_counters_label) {
        char tmp[64];
        std::snprintf(tmp, sizeof(tmp), "OK %lu  /  ERR %lu",
                      static_cast<unsigned long>(abarth::obd::obd().successfulReads()),
                      static_cast<unsigned long>(abarth::obd::obd().failedReads()));
        lv_label_set_text(g_counters_label, tmp);
    }
}

}  // namespace

void build() {
    // LVGL gira in un task dedicato (port v8): ogni accesso alle API LVGL va
    // fatto con il mutex preso.
    if (!lvgl_port_lock(-1)) {
        ESP_LOGE(TAG, "lvgl_port_lock fallito");
        return;
    }

    ensure_styles();

    lv_obj_t* scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0B0B10), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    /* --- Header ----------------------------------------------------------- */
    lv_obj_t* header = lv_obj_create(scr);
    lv_obj_remove_style_all(header);
    lv_obj_set_size(header, LV_PCT(100), 44);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x14141A), 0);
    lv_obj_set_style_bg_opa(header, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(header, 16, 0);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* title = lv_label_create(header);
    lv_obj_set_style_text_color(title, lv_color_hex(0xCC3030), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_label_set_text(title, "ABARTH 595");

    g_state_label = lv_label_create(header);
    lv_obj_set_style_text_color(g_state_label, lv_color_hex(0xDEDEDE), 0);
    lv_obj_set_style_text_font(g_state_label, &lv_font_montserrat_16, 0);
    lv_label_set_text(g_state_label, "—");

    /* --- Tabview ---------------------------------------------------------- */
    lv_obj_t* tabview = lv_tabview_create(scr, LV_DIR_TOP, 48);
    lv_obj_set_size(tabview, LV_PCT(100), LV_VER_RES - 44);
    lv_obj_align(tabview, LV_ALIGN_BOTTOM_MID, 0, 0);

    lv_obj_t* t_motor = lv_tabview_add_tab(tabview, "Motore");
    lv_obj_t* t_temp  = lv_tabview_add_tab(tabview, "Temperature");
    lv_obj_t* t_pres  = lv_tabview_add_tab(tabview, "Pressioni");
    lv_obj_t* t_elec  = lv_tabview_add_tab(tabview, "Elettrico");
    lv_obj_t* t_info  = lv_tabview_add_tab(tabview, "Stato");

    /* Motore ---------------------------------------------------------------- */
    setup_grid_tab(t_motor);
    auto& cRpm  = add_card(t_motor, Metric::Rpm,        "Giri motore",   "RPM",  0);
    auto& cSpd  = add_card(t_motor, Metric::Speed,      "Velocita'",     "km/h", 0);
    auto& cLoad = add_card(t_motor, Metric::EngineLoad, "Carico motore", "%",    0);
    auto& cThr  = add_card(t_motor, Metric::Throttle,   "Acceleratore",  "%",    0);
    set_card_size(cRpm, 2, 2);  set_card_size(cSpd, 2, 2);
    set_card_size(cLoad, 2, 2); set_card_size(cThr, 2, 2);

    /* Temperature ----------------------------------------------------------- */
    setup_grid_tab(t_temp);
    auto& cCool = add_card(t_temp, Metric::CoolantTempC,   "Temp. liquido", "°C", 0);
    auto& cOil  = add_card(t_temp, Metric::OilTempC,       "Temp. olio",    "°C", 0);
    auto& cIat  = add_card(t_temp, Metric::IntakeAirTempC, "Aria aspirata", "°C", 0);
    auto& cAmb  = add_card(t_temp, Metric::AmbientTempC,   "Temp. esterna", "°C", 0);
    auto& cEgt  = add_card(t_temp, Metric::EgtC,           "Gas scarico",   "°C", 0);
    set_card_size(cCool, 3, 2); set_card_size(cOil, 3, 2); set_card_size(cIat, 3, 2);
    set_card_size(cAmb, 3, 2);  set_card_size(cEgt, 3, 2);

    /* Pressioni ------------------------------------------------------------- */
    setup_grid_tab(t_pres);
    auto& cBoostR = add_card(t_pres, Metric::BoostRelBar,    "Boost",       "bar", 2);
    auto& cMap    = add_card(t_pres, Metric::BoostKpa,       "MAP",         "kPa", 0);
    auto& cOilP   = add_card(t_pres, Metric::OilPressureBar, "Press. olio", "bar", 1);
    set_card_size(cBoostR, 3, 1); set_card_size(cMap, 3, 1); set_card_size(cOilP, 3, 1);

    /* Elettrico ------------------------------------------------------------- */
    setup_grid_tab(t_elec);
    auto& cBat = add_card(t_elec, Metric::BatteryVolts, "Batteria",      "V",  2);
    auto& cAfr = add_card(t_elec, Metric::Afr,          "Miscela (AFR)", "",   2);
    set_card_size(cBat, 2, 1); set_card_size(cAfr, 2, 1);

    /* Stato ---------------------------------------------------------------- */
    lv_obj_set_style_pad_all(t_info, 24, 0);
    lv_obj_set_flex_flow(t_info, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(t_info, 16, 0);

    lv_obj_t* s1 = lv_label_create(t_info);
    lv_obj_set_style_text_font(s1, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s1, lv_color_hex(0xDEDEDE), 0);
    lv_label_set_text(s1, "Dongle OBD: vLinker FD (BLE)");

    g_counters_label = lv_label_create(t_info);
    lv_obj_set_style_text_font(g_counters_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(g_counters_label, lv_color_hex(0xF8F8F8), 0);
    lv_label_set_text(g_counters_label, "OK 0  /  ERR 0");

    lv_obj_t* hint = lv_label_create(t_info);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x9A9AA6), 0);
    lv_label_set_text(hint,
        "Se un valore resta '—' significa che il PID non e' supportato dalla\n"
        "centralina o che il codice nel file src/obd/pids.cpp va aggiornato.\n"
        "Verifica i PID Mode 22 (olio, boost reale, pressione olio) con un\n"
        "tool come Car Scanner o FiatECUScan sulla tua specifica 595.");

    lv_timer_create(&on_tick, abarth::config::kUiUpdateMs, nullptr);

    lvgl_port_unlock();
}

}  // namespace abarth::ui
