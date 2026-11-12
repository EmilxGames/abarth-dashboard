#include "ui_v2/alert_banner.h"

#include "config/settings.h"
#include "ui_v2/theme.h"
#include "ui_v2/widgets.h"

namespace abarth::ui_v2 {
using namespace theme;

namespace { bool g_v2_built = false; }
void markBuilt() { g_v2_built = true; }

bool alertBannerActive() {
    return g_v2_built && settings::current().ui_style == settings::UiStyle::Racing;
}

void buildAlertBanner(lv_obj_t* host, const char* title, const char* msg, AlertLevel level,
                      const char* ok_label, bool countdown, lv_obj_t** out_timer, lv_event_cb_t on_ok) {
    const uint32_t col = level == AlertLevel::Critical ? kCrit : level == AlertLevel::Warning ? kWarn : kOk;
    const int32_t wdt = LV_HOR_RES - 2 * kMargin;
    // altezza fissa calcolata sul testo (nessun layout dinamico)
    lv_point_t sz;
    lv_text_get_size(&sz, msg, fontUnit(), 0, 4, wdt - 2 * kCardPad, LV_TEXT_FLAG_NONE);
    const int32_t h = kCardPad + 34 + sz.y + 20 + 60 + kCardPad;
    lv_obj_set_style_pad_top(host, 10, 0);
    lv_obj_t* card = lv_obj_create(host);
    lv_obj_remove_style_all(card);
    lv_obj_set_size(card, wdt, h);
    lv_obj_set_style_bg_color(card, c(kCard), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, kRadius, 0);
    lv_obj_set_style_border_width(card, 2, 0);
    lv_obj_set_style_border_color(card, c(col), 0);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    w::box(card, kCardPad, kCardPad + 8, kStatusDot, kStatusDot, col, LV_RADIUS_CIRCLE);
    lv_obj_t* t = w::label(card, title, fontBody(), kText);
    lv_obj_set_pos(t, kCardPad + kStatusDot + 12, kCardPad - 2);
    lv_obj_t* m = w::label(card, msg, fontUnit(), kSecondary);
    lv_obj_set_style_text_line_space(m, 4, 0);
    lv_label_set_long_mode(m, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(m, wdt - 2 * kCardPad);
    lv_obj_set_pos(m, kCardPad, kCardPad + 34);
    const int32_t bw = 200;
    lv_obj_t* b = w::box(card, wdt - kCardPad - bw, h - kCardPad - 56, bw, 56, col, 18);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(b, c(kCardPress), LV_STATE_PRESSED);
    lv_obj_t* lbl = w::label(b, ok_label, fontBody(), level == AlertLevel::Info || level == AlertLevel::Warning ? kBg : kText);
    lv_obj_align(lbl, LV_ALIGN_CENTER, countdown ? -22 : 0, 0);
    if (countdown && out_timer) {
        *out_timer = w::label(b, "", fontUnit(), level == AlertLevel::Critical ? kText : kBg);
        lv_obj_align(*out_timer, LV_ALIGN_RIGHT_MID, -16, 0);
    }
    lv_obj_add_event_cb(b, on_ok, LV_EVENT_CLICKED, nullptr);
}
}  // namespace abarth::ui_v2
