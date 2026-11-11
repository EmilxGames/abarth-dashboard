#include "ui_v2/pages.h"

#include <esp_system.h>
#include "lvgl.h"
#include "config/settings.h"
#include "ui/settings_tab.h"
#include "ui_v2/page_grid.h"
#include "ui_v2/page_guida.h"
#include "ui_v2/page_personale.h"
#include "ui_v2/page_setup.h"
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

}  // namespace

void build(Page p, lv_obj_t* parent, int32_t w, int32_t h, int32_t y0) {
    switch (p) {
        case Page::Guida:       guida::build(parent, w, h, y0); break;
        case Page::Temperature: g_temp = grid::create(parent, w, y0, kTempDef); break;
        case Page::Pressioni:   g_press = grid::create(parent, w, y0, kPressDef); break;
        case Page::Elettrico:   g_elec = grid::create(parent, w, y0, kElecDef); break;
        case Page::Personale:   personale::build(parent, w, h, y0); break;
        case Page::Setup:       setup_page::build(parent, w, h); break;
        default: break;
    }
}

void update(Page p, int64_t now_ms) {
    if (p == Page::Guida) guida::update(now_ms);
    else if (p == Page::Temperature) grid::update(g_temp, now_ms);
    else if (p == Page::Pressioni) grid::update(g_press, now_ms);
    else if (p == Page::Elettrico) grid::update(g_elec, now_ms);
    else if (p == Page::Personale) personale::update(now_ms);
    else if (p == Page::Setup) setup_page::update(now_ms);
}

void onShow(Page) {}

}  // namespace abarth::ui_v2::pages
