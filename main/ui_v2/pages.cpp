#include "ui_v2/pages.h"

#include <esp_system.h>
#include "lvgl.h"
#include "config/settings.h"
#include "ui/settings_tab.h"
#include "ui_v2/page_grid.h"
#include "ui_v2/page_guida.h"
#include "ui_v2/page_personale.h"
#include "ui_v2/theme.h"
#include "ui_v2/widgets.h"

namespace abarth::ui_v2::pages {

namespace {
using namespace theme;

using data::Metric;
grid::GridPage* g_temp = nullptr;
grid::GridPage* g_press = nullptr;
grid::GridPage* g_elec = nullptr;

const grid::PageDef kTempDef  = {true, {Metric::CoolantTempC, Metric::OilTempC, Metric::IntakeAirTempC,
                                        Metric::AmbientTempC, Metric::EgtC, Metric::FuelTempC, Metric::Rpm}, 6};
const grid::PageDef kPressDef = {true, {Metric::BoostRelBar, Metric::BoostKpa, Metric::OilPressureBar,
                                        Metric::FuelPressureKpa, Metric::EngineLoad, Metric::Throttle, Metric::Rpm}, 6};
const grid::PageDef kElecDef  = {true, {Metric::FuelLevelPct, Metric::BatteryVolts, Metric::Afr,
                                        Metric::FuelConsumptionL100, Metric::FuelRateLh,
                                        Metric::FuelInjectionTiming, Metric::DistanceSinceClear}, 7};

lv_obj_t* g_setup_page = nullptr;
lv_obj_t* g_adv_host = nullptr;   ///< Impostazioni avanzate (quelle complete della Classica)
int32_t g_w = 0, g_h = 0;

void placeholder(lv_obj_t* parent, const char* txt) {
    lv_obj_t* l = w::label(parent, txt, fontLabel(), kDim, true);
    lv_obj_center(l);
}

void adv_close_cb(lv_event_t*) {
    if (g_adv_host) lv_obj_add_flag(g_adv_host, LV_OBJ_FLAG_HIDDEN);
}

void adv_open_cb(lv_event_t*) {
    if (!g_adv_host) {
        g_adv_host = w::box(g_setup_page, 0, 0, g_w, g_h, kBg);
        lv_obj_t* back = w::box(g_adv_host, 0, 0, g_w, 56, kCard);
        lv_obj_add_flag(back, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_t* bl = w::label(back, LV_SYMBOL_LEFT "  Indietro", fontBody(), kText);
        lv_obj_align(bl, LV_ALIGN_LEFT_MID, kCardPad, 0);
        lv_obj_add_event_cb(back, adv_close_cb, LV_EVENT_CLICKED, nullptr);
        lv_obj_t* host = w::box(g_adv_host, 0, 56, g_w, g_h - 56, kBg);
        lv_obj_remove_flag(host, LV_OBJ_FLAG_GESTURE_BUBBLE);
        ui::settings_tab::build(host);
    }
    lv_obj_remove_flag(g_adv_host, LV_OBJ_FLAG_HIDDEN);
}

void build_setup(lv_obj_t* parent) {
    // Provvisoria: la pagina Impostazioni completa arriva nello step 4
    g_setup_page = parent;
    lv_obj_t* card = w::card(parent, kMargin, 10, g_w - 2 * kMargin, kRowH);
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(card, c(kCardPress), LV_STATE_PRESSED);
    lv_obj_t* l = w::label(card, "Impostazioni avanzate", fontBody(), kText);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, kCardPad, 0);
    lv_obj_add_event_cb(card, adv_open_cb, LV_EVENT_CLICKED, nullptr);
}
}  // namespace

void build(Page p, lv_obj_t* parent, int32_t w, int32_t h, int32_t y0) {
    g_w = w;
    g_h = h;
    switch (p) {
        case Page::Guida:       guida::build(parent, w, h, y0); break;
        case Page::Temperature: g_temp = grid::create(parent, w, y0, kTempDef); break;
        case Page::Pressioni:   g_press = grid::create(parent, w, y0, kPressDef); break;
        case Page::Elettrico:   g_elec = grid::create(parent, w, y0, kElecDef); break;
        case Page::Personale:   personale::build(parent, w, h, y0); break;
        case Page::Setup:       build_setup(parent); break;
        default: break;
    }
}

void update(Page p, int64_t now_ms) {
    if (p == Page::Guida) guida::update(now_ms);
    else if (p == Page::Temperature) grid::update(g_temp, now_ms);
    else if (p == Page::Pressioni) grid::update(g_press, now_ms);
    else if (p == Page::Elettrico) grid::update(g_elec, now_ms);
    else if (p == Page::Personale) personale::update(now_ms);
    if (p == Page::Setup && g_adv_host && !lv_obj_has_flag(g_adv_host, LV_OBJ_FLAG_HIDDEN)) {
        static uint32_t n = 0;
        if ((n++ % 2) == 0) ui::settings_tab::tick();
    }
}

void onShow(Page) {}

}  // namespace abarth::ui_v2::pages
