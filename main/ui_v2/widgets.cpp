#include "ui_v2/widgets.h"

#include <cmath>
#include <cstring>
#include "ui_v2/data_fmt.h"
#include "ui_v2/theme.h"

namespace abarth::ui_v2::w {

using namespace theme;

lv_obj_t* box(lv_obj_t* parent, int32_t x, int32_t y, int32_t wdt, int32_t hgt, uint32_t bg, int32_t radius) {
    lv_obj_t* o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, wdt, hgt);
    lv_obj_set_style_bg_color(o, c(bg), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    if (radius) lv_obj_set_style_radius(o, radius, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(o, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_flag(o, LV_OBJ_FLAG_EVENT_BUBBLE);
    return o;
}

lv_obj_t* card(lv_obj_t* parent, int32_t x, int32_t y, int32_t wdt, int32_t hgt) {
    return box(parent, x, y, wdt, hgt, kCard, kRadius);
}

lv_obj_t* label(lv_obj_t* parent, const char* txt, const lv_font_t* font, uint32_t color, bool upper_label) {
    lv_obj_t* l = lv_label_create(parent);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, c(color), 0);
    if (upper_label) lv_obj_set_style_text_letter_space(l, kLabelLetterSpace, 0);
    lv_obj_add_flag(l, LV_OBJ_FLAG_GESTURE_BUBBLE);
    return l;
}

void setText(lv_obj_t* lbl, const char* txt) {
    if (std::strcmp(lv_label_get_text(lbl), txt) != 0) lv_label_set_text(lbl, txt);
}

void setColor(lv_obj_t* lbl, uint32_t color, uint32_t& cache) {
    if (cache == color) return;
    cache = color;
    lv_obj_set_style_text_color(lbl, c(color), 0);
}

int32_t textWidth(const char* txt, const lv_font_t* font) {
    lv_point_t p;
    lv_text_get_size(&p, txt, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    return p.x;
}

uint32_t levelColor(Level lv) {
    switch (lv) {
        case Level::Warn:   return kCrit;
        case Level::Low:    return kWarn;
        case Level::Normal: return kOk;
        default:            return kDim;
    }
}

// ---------------------------------------------------------------- PillBar
void PillBar::create(lv_obj_t* parent, int32_t x, int32_t y, int32_t wdt) {
    w_ = wdt;
    box(parent, x, y, wdt, kPillH, kTrack, kPillH / 2 + 6);
    fill_ = box(parent, x, y, kPillH, kPillH, accent(), kPillH / 2 + 6);
    color_ = accent();
    lv_obj_add_flag(fill_, LV_OBJ_FLAG_HIDDEN);
}

void PillBar::set(float fraction, uint32_t color) {
    int32_t px = 0;
    if (!std::isnan(fraction)) {
        if (fraction < 0) fraction = 0;
        if (fraction > 1) fraction = 1;
        px = static_cast<int32_t>(fraction * w_ + 0.5f);
        if (px > 0 && px < kPillH) px = kPillH;  // forma a pillola anche quando e' corta
    }
    if (color != color_) {
        color_ = color;
        lv_obj_set_style_bg_color(fill_, c(color), 0);
    }
    if (px == last_w_) return;
    last_w_ = px;
    if (px == 0) {
        lv_obj_add_flag(fill_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(fill_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_width(fill_, px);
    }
}

// ---------------------------------------------------------------- StatusDot
void StatusDot::create(lv_obj_t* parent, int32_t x, int32_t y) {
    dot_ = box(parent, x, y, kStatusDot, kStatusDot, kOk, LV_RADIUS_CIRCLE);
    lv_obj_add_flag(dot_, LV_OBJ_FLAG_HIDDEN);
    vis_ = 0;
}

void StatusDot::set(bool visible, uint32_t color) {
    if ((visible ? 1 : 0) != vis_) {
        vis_ = visible ? 1 : 0;
        if (visible) lv_obj_remove_flag(dot_, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(dot_, LV_OBJ_FLAG_HIDDEN);
    }
    if (visible && color != color_) {
        color_ = color;
        lv_obj_set_style_bg_color(dot_, c(color), 0);
    }
}

// ---------------------------------------------------------------- ValueCard
void ValueCard::create(lv_obj_t* parent, int32_t x, int32_t y, int32_t wdt, int32_t hgt,
                       const char* title, bool show_dot) {
    card_ = card(parent, x, y, wdt, hgt);
    lv_obj_t* t = label(card_, title, fontLabel(), kDim, true);
    lv_obj_set_pos(t, kCardPad, kCardPad);
    has_dot_ = show_dot;
    if (show_dot) dot_.create(card_, wdt - kCardPad - kStatusDot, kCardPad + 3);
    // valore in basso a sinistra, lontano dagli angoli arrotondati
    value_x_ = kCardPad;
    value_y_ = hgt - kCardPad - lv_font_get_line_height(fontValue());
    value_ = label(card_, fmt::kNoData, fontValue(), kDim);  // "–"
    lv_obj_set_pos(value_, value_x_, value_y_);
    value_color_ = kDim;
    unit_ = label(card_, "", fontUnit(), kDim);
}

void ValueCard::set(const Binding& b, Level dot_level) {
    const bool missing = b.level == Level::Missing;
    const char* txt = missing ? fmt::kNoData : b.text;
    const bool changed = std::strcmp(lv_label_get_text(value_), txt) != 0;
    if (changed) lv_label_set_text(value_, txt);
    setColor(value_, missing ? kDim : kText, value_color_);
    if (changed || std::strcmp(lv_label_get_text(unit_), b.unit) != 0) {
        lv_label_set_text(unit_, b.unit);
        // unita' subito dopo il valore, allineata alla base
        const int32_t vx = value_x_ + textWidth(txt, fontValue()) + 8;
        // allinea le linee di base: base = top + line_height - base_line
        const int32_t base = value_y_ + lv_font_get_line_height(fontValue()) - fontValue()->base_line;
        const int32_t uy = base - (lv_font_get_line_height(fontUnit()) - fontUnit()->base_line);
        lv_obj_set_pos(unit_, vx, uy);
    }
    if (has_dot_) dot_.set(!missing, levelColor(dot_level));
}

// ---------------------------------------------------------------- WideCard
void WideCard::create(lv_obj_t* parent, int32_t x, int32_t y, int32_t wdt, int32_t hgt,
                      const char* title, bool show_dot) {
    card_ = card(parent, x, y, wdt, hgt);
    lv_obj_t* t = label(card_, title, fontLabel(), kDim, true);
    lv_obj_set_pos(t, kCardPad, kCardPad);
    has_dot_ = show_dot;
    if (show_dot) dot_.create(card_, wdt - kCardPad - kStatusDot, kCardPad + 3);
    const int32_t bar_y = hgt - kCardPad - kPillH;
    bar_.create(card_, kCardPad, bar_y, wdt - 2 * kCardPad);
    value_y_ = bar_y - 16 - lv_font_get_line_height(fontValue());
    value_ = label(card_, fmt::kNoData, fontValue(), kDim);
    lv_obj_set_pos(value_, kCardPad, value_y_);
    value_color_ = kDim;
    unit_ = label(card_, "", fontUnit(), kDim);
}

void WideCard::set(const Binding& b, Level dot_level) {
    const bool missing = b.level == Level::Missing;
    const char* txt = missing ? fmt::kNoData : b.text;
    const bool changed = std::strcmp(lv_label_get_text(value_), txt) != 0;
    if (changed) lv_label_set_text(value_, txt);
    setColor(value_, missing ? kDim : kText, value_color_);
    if (changed || std::strcmp(lv_label_get_text(unit_), b.unit) != 0) {
        lv_label_set_text(unit_, b.unit);
        const int32_t base = value_y_ + lv_font_get_line_height(fontValue()) - fontValue()->base_line;
        lv_obj_set_pos(unit_, kCardPad + textWidth(txt, fontValue()) + 8,
                       base - (lv_font_get_line_height(fontUnit()) - fontUnit()->base_line));
    }
    const bool crit = !missing && dot_level == Level::Warn;
    bar_.set(missing ? NAN : b.fraction(), crit ? kCrit : accent());
    if (has_dot_) dot_.set(!missing, levelColor(dot_level));
}

}  // namespace abarth::ui_v2::w
