// Pagina GUIDA: giri (barra + 7 segmenti), velocita', turbo, acqua, olio, batteria.
#include <cstdio>
#include "lvgl.h"
#include "config/settings.h"
#include "config/warmup_manager.h"
#include "ui_v2/data_fmt.h"
#include "ui_v2/page_guida.h"
#include "ui_v2/theme.h"
#include "ui_v2/widgets.h"

namespace abarth::ui_v2::guida {

namespace {
using namespace theme;
using data::Metric;

constexpr float kRpmMax      = 7000.0f;  ///< fondo scala barra giri
constexpr float kShiftRpm    = 6200.0f;  ///< da qui lampeggia (limitatore)

w::SegBar   g_rpm_bar;
w::SevenSeg g_rpm;
w::SevenSeg g_speed;
lv_obj_t*   g_speed_unit = nullptr;
lv_obj_t*   g_boost_val = nullptr;
lv_obj_t*   g_boost_unit = nullptr;
uint32_t    g_boost_col = 0;
w::HBar     g_boost_bar;
w::Tile     g_water, g_oil, g_batt;
lv_obj_t*   g_thr_val = nullptr;
w::HBar     g_thr_bar;
}  // namespace

void build(lv_obj_t* p, int32_t W, int32_t H) {
    const int32_t m = 16;          // margine laterale
    const int32_t inner = W - 2 * m;

    // 1. Barra giri a segmenti
    int32_t y = 14;
    g_rpm_bar.create(p, m, y, inner, 64, 40, 4, 0.65f, 0.85f);
    y += 64 + 22;

    // 2. Giri grandi a 7 segmenti, centrati
    const int32_t dh = 176, dw = 100, t = 18, gap = 18;
    const int32_t rpm_w = 4 * dw + 3 * gap;
    g_rpm.create(p, (W - rpm_w) / 2, y, 4, dw, dh, t, gap, kText, kSegOff);
    lv_obj_t* l = w::label(p, "GIRI / MIN", fontSmall(), kMuted);
    lv_obj_set_pos(l, (W - rpm_w) / 2, y + dh + 8);
    y += dh + 40;

    // 3. Velocita' e turbo
    const int32_t half_w = (inner - kGap) / 2;
    const int32_t row_h = 210;
    lv_obj_t* sp = w::box(p, m, y, half_w, row_h, kPanel);
    w::box(sp, 0, 0, 4, row_h, kRed);
    lv_obj_t* st = w::label(sp, "VELOCITA'", fontSmall(), kMuted);
    lv_obj_set_pos(st, kPad + 4, kPad - 2);
    const int32_t sdh = 120, sdw = 66, st_t = 14, sgap = 12;
    const int32_t sp_w = 3 * sdw + 2 * sgap;
    g_speed.create(sp, (half_w - sp_w) / 2 + 2, 44, 3, sdw, sdh, st_t, sgap, kText, kSegOff);
    g_speed_unit = w::label(sp, "", fontSmall(), kMuted);
    lv_obj_align(g_speed_unit, LV_ALIGN_BOTTOM_RIGHT, -kPad, -kPad + 2);

    lv_obj_t* bo = w::box(p, m + half_w + kGap, y, half_w, row_h, kPanel);
    w::box(bo, 0, 0, 4, row_h, kRed);
    lv_obj_t* bt = w::label(bo, "TURBO", fontSmall(), kMuted);
    lv_obj_set_pos(bt, kPad + 4, kPad - 2);
    g_boost_val = w::label(bo, "--", fontHuge(), kText);
    lv_obj_set_pos(g_boost_val, kPad + 4, 64);
    g_boost_unit = w::label(bo, "bar", fontSmall(), kMuted);
    lv_obj_align(g_boost_unit, LV_ALIGN_TOP_RIGHT, -kPad, kPad - 2);
    g_boost_bar.create(bo, kPad + 4, row_h - 34, half_w - 2 * kPad - 4, 14, -0.5f, 1.8f, kRed);
    y += row_h + kGap;

    // 4. Acqua, olio, batteria
    const int32_t tw = (inner - 2 * kGap) / 3;
    const int32_t th = 190;
    g_water.create(p, m, y, tw, th, "ACQUA", fontHuge());
    g_oil.create(p, m + tw + kGap, y, tw, th, "OLIO", fontHuge());
    g_batt.create(p, m + 2 * (tw + kGap), y, inner - 2 * (tw + kGap), th, "BATTERIA", fontHuge());
    y += th + kGap;

    // 5. Acceleratore (barra a tutta larghezza nello spazio rimanente)
    const int32_t ah = H - y - 14;
    lv_obj_t* ac = w::box(p, m, y, inner, ah, kPanel);
    w::box(ac, 0, 0, 4, ah, kRed);
    lv_obj_t* at = w::label(ac, "ACCELERATORE", fontSmall(), kMuted);
    lv_obj_set_pos(at, kPad + 4, kPad - 2);
    g_thr_val = w::label(ac, "--", fontMedium(), kText);
    lv_obj_align(g_thr_val, LV_ALIGN_TOP_RIGHT, -kPad, kPad - 6);
    g_thr_bar.create(ac, kPad + 4, ah - 30, inner - 2 * kPad - 4, 16, 0.0f, 100.0f, kGreen);
}

void update(int64_t now_ms) {
    float v = 0;
    char buf[16];
    const char* unit = "";

    // Giri
    const bool rpm_ok = fmt::get(Metric::Rpm, now_ms, v);
    const float rpm = rpm_ok ? v : 0.0f;
    const bool flash = rpm_ok && rpm >= kShiftRpm;
    const bool phase = ((now_ms / 125) & 1) != 0;   // 4 Hz
    g_rpm_bar.set(rpm / kRpmMax, flash, phase);
    g_rpm.set(rpm_ok ? static_cast<int32_t>(rpm / 10.0f + 0.5f) * 10 : -1);  // passo 10 giri
    g_rpm.setOnColor(flash ? kRed : kText);

    // Velocita'
    if (fmt::get(Metric::Speed, now_ms, v)) {
        g_speed.set(static_cast<int32_t>(settings::speedInCurrentUnit(v) + 0.5f));
    } else {
        g_speed.set(-1);
    }
    w::setText(g_speed_unit, settings::speedUnitSymbol());

    // Turbo (bar relativi)
    const bool b_ok = fmt::get(Metric::BoostRelBar, now_ms, v);
    if (b_ok) {
        std::snprintf(buf, sizeof(buf), "%+.2f", v);
        w::setText(g_boost_val, buf);
        w::setColor(g_boost_val, v > 1.5f ? kRed : kText, g_boost_col);
    } else {
        w::setText(g_boost_val, "--");
        w::setColor(g_boost_val, kMuted, g_boost_col);
    }
    g_boost_bar.set(v, b_ok);

    // Acqua
    if (fmt::get(Metric::CoolantTempC, now_ms, v)) {
        fmt::format(fmt::Kind::TempC, v, 0, "", buf, sizeof(buf), unit);
        g_water.set(buf, unit, v > 105.0f ? kRed : (v < 60.0f ? kBlue : kText));
    } else {
        g_water.set("--", settings::tempUnitSymbol(), kMuted);
    }
    // Olio: reale se disponibile, altrimenti stima del warmup
    if (fmt::get(Metric::OilTempC, now_ms, v)) {
        fmt::format(fmt::Kind::TempC, v, 0, "", buf, sizeof(buf), unit);
        g_oil.set(buf, unit, v > 125.0f ? kRed : (v < 70.0f ? kBlue : kText));
    } else {
        g_oil.set("--", settings::tempUnitSymbol(), kMuted);
    }
    // Acceleratore
    const bool t_ok = fmt::get(Metric::Throttle, now_ms, v);
    if (t_ok) {
        std::snprintf(buf, sizeof(buf), "%.0f %%", v);
        w::setText(g_thr_val, buf);
    } else {
        w::setText(g_thr_val, "--");
    }
    g_thr_bar.set(static_cast<float>(static_cast<int>(v)), t_ok);  // passo 1 %

    // Batteria
    if (fmt::get(Metric::BatteryVolts, now_ms, v)) {
        std::snprintf(buf, sizeof(buf), "%.1f", v);
        g_batt.set(buf, "V", (v < 12.0f || v > 15.0f) ? kYellow : kText);
    } else {
        g_batt.set("--", "V", kMuted);
    }
}

}  // namespace abarth::ui_v2::guida
