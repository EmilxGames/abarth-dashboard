// Pagina GUIDA (design "Schede"): velocita' grande + giri con barra a pillola,
// poi Boost / Acqua e Olio / Batteria. Solo valori, barre e pallini si ridisegnano.
#include <cmath>
#include <cstdio>
#include "lvgl.h"
#include "config/settings.h"
#include "ui_v2/alert_levels.h"
#include "ui_v2/binding.h"
#include "ui_v2/page_guida.h"
#include "ui_v2/data_fmt.h"
#include "ui_v2/theme.h"
#include "ui_v2/widgets.h"

namespace abarth::ui_v2::guida {

namespace {
using namespace theme;
using data::Metric;

lv_obj_t*    g_speed = nullptr;
lv_obj_t*    g_speed_unit = nullptr;
lv_obj_t*    g_rpm = nullptr;
uint32_t     g_speed_col = 0, g_rpm_col = 0;
w::PillBar   g_rpm_bar;
w::ValueCard g_boost, g_water, g_oil, g_batt;
Binding      b_speed, b_rpm, b_boost, b_water, b_oil, b_batt;
}  // namespace

void build(lv_obj_t* p, int32_t W, int32_t /*H*/, int32_t y0) {
    // Le misure del design sono in coordinate schermo: y0 = top della pagina
    const int32_t cw = W - 2 * kMargin;
    const int32_t total = kCardsBot - kCardsTop - 2 * kGap;
    const int32_t small_h = static_cast<int32_t>(total / 3.55f);
    const int32_t hero_h = total - 2 * small_h;
    int32_t y = kCardsTop - y0;

    // --- Scheda principale: velocita' + giri
    lv_obj_t* hero = w::card(p, kMargin, y, cw, hero_h);
    lv_obj_t* l = w::label(hero, "VELOCITA'", fontLabel(), kDim, true);
    lv_obj_set_pos(l, kCardPad, kCardPad);
    g_speed_unit = w::label(hero, "KM/H", fontLabel(), kDim, true);
    lv_obj_align(g_speed_unit, LV_ALIGN_TOP_RIGHT, -kCardPad, kCardPad);
    g_speed = w::label(hero, fmt::kNoData, fontHero(), kDim);
    lv_obj_set_pos(g_speed, kCardPad - 6, kCardPad + 26);
    g_speed_col = kDim;

    const int32_t bar_y = hero_h - kCardPad - kPillH;
    g_rpm_bar.create(hero, kCardPad, bar_y, cw - 2 * kCardPad);
    const int32_t row_y = bar_y - 14 - lv_font_get_line_height(fontMedium());
    lv_obj_t* gl = w::label(hero, "GIRI", fontLabel(), kDim, true);
    lv_obj_set_pos(gl, kCardPad, row_y + (lv_font_get_line_height(fontMedium()) - lv_font_get_line_height(fontLabel())) / 2);
    g_rpm = w::label(hero, fmt::kNoData, fontMedium(), kDim);
    lv_obj_set_style_text_align(g_rpm, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_width(g_rpm, 220);  // riquadro fisso: le cifre non spostano nulla
    lv_obj_set_pos(g_rpm, cw - kCardPad - 220, row_y);
    g_rpm_col = kDim;
    y += hero_h + kGap;

    // --- Due righe di schede piccole
    const int32_t half = (cw - kGap) / 2;
    g_boost.create(p, kMargin, y, half, small_h, "BOOST", true);
    g_water.create(p, kMargin + half + kGap, y, cw - half - kGap, small_h, "ACQUA", true);
    y += small_h + kGap;
    g_oil.create(p, kMargin, y, half, small_h, "OLIO", true);
    g_batt.create(p, kMargin + half + kGap, y, cw - half - kGap, small_h, "BATTERIA", true);

    b_speed.bind(Metric::Speed);
    b_rpm.bind(Metric::Rpm);
    b_boost.bind(Metric::BoostRelBar);
    b_water.bind(Metric::CoolantTempC);
    b_oil.bind(Metric::OilTempC);
    b_batt.bind(Metric::BatteryVolts);
}

void update(int64_t now_ms) {
    // Velocita'
    if (b_speed.update(now_ms)) {
        w::setText(g_speed, b_speed.level == Level::Missing ? fmt::kNoData : b_speed.text);
        w::setColor(g_speed, b_speed.level == Level::Missing ? kDim : kText, g_speed_col);
        char u[12];
        std::snprintf(u, sizeof(u), "%s", b_speed.unit);
        for (char* c = u; *c; ++c) if (*c >= 'a' && *c <= 'z') *c = *c - 'a' + 'A';
        w::setText(g_speed_unit, u);
    }

    // Giri: valore a passi di 10, barra rossa dalla soglia configurata
    const float redline = settings::current().redline_rpm;
    b_rpm.update(now_ms);
    const bool rpm_ok = b_rpm.level != Level::Missing;
    char rbuf[12];
    if (rpm_ok) std::snprintf(rbuf, sizeof(rbuf), "%d", static_cast<int>(b_rpm.value / 10.0f + 0.5f) * 10);
    w::setText(g_rpm, rpm_ok ? rbuf : fmt::kNoData);
    const bool red = rpm_ok && b_rpm.value >= redline;
    w::setColor(g_rpm, !rpm_ok ? kDim : (red ? kCrit : kText), g_rpm_col);
    g_rpm_bar.set(rpm_ok ? b_rpm.value / 7000.0f : NAN, red ? kCrit : accent());

    // Schede piccole con pallino dalle soglie d'allarme
    auto upd = [&](Binding& b, w::ValueCard& card) {
        if (b.update(now_ms) || true) {
            card.set(b, b.level == Level::Missing ? Level::Missing : alertLevel(b.info->metric, b.value));
        }
    };
    upd(b_boost, g_boost);
    upd(b_water, g_water);
    upd(b_oil, g_oil);
    upd(b_batt, g_batt);
}

}  // namespace abarth::ui_v2::guida
