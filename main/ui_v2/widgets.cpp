#include "ui_v2/widgets.h"

#include <cstring>
#include "ui_v2/theme.h"

namespace abarth::ui_v2::w {

using namespace theme;

lv_obj_t* box(lv_obj_t* parent, int32_t x, int32_t y, int32_t wdt, int32_t hgt, uint32_t bg) {
    lv_obj_t* o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, wdt, hgt);
    lv_obj_set_style_bg_color(o, c(bg), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    return o;
}

lv_obj_t* label(lv_obj_t* parent, const char* txt, const lv_font_t* font, uint32_t color) {
    lv_obj_t* l = lv_label_create(parent);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, c(color), 0);
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

// ---------------------------------------------------------------- SevenSeg
namespace {
// bit: a b c d e f g
constexpr uint8_t kDigitMask[10] = {
    0b1111110, 0b0110000, 0b1101101, 0b1111001, 0b0110011,
    0b1011011, 0b1011111, 0b1110000, 0b1111111, 0b1111011,
};
constexpr uint8_t kDash = 0b0000001;
}  // namespace

void SevenSeg::create(lv_obj_t* parent, int32_t x, int32_t y, int digits, int32_t dw, int32_t dh,
                      int32_t t, int32_t gap, uint32_t on_color, uint32_t off_color) {
    digits_ = digits > kMaxDigits ? kMaxDigits : digits;
    on_ = on_color;
    off_ = off_color;
    width_ = digits_ * dw + (digits_ - 1) * gap;
    const int32_t half = dh / 2;
    const int32_t vlen = half - t - t / 2;  // lunghezza segmenti verticali
    for (int d = 0; d < digits_; ++d) {
        const int32_t dx = x + d * (dw + gap);
        const int32_t r[7][4] = {
            {dx + t, y, dw - 2 * t, t},                         // a
            {dx + dw - t, y + t, t, vlen},                      // b
            {dx + dw - t, y + half + t / 2, t, vlen},           // c
            {dx + t, y + dh - t, dw - 2 * t, t},                // d
            {dx, y + half + t / 2, t, vlen},                    // e
            {dx, y + t, t, vlen},                               // f
            {dx + t, y + half - t / 2, dw - 2 * t, t},          // g
        };
        for (int s = 0; s < 7; ++s) {
            seg_[d][s] = box(parent, r[s][0], r[s][1], r[s][2], r[s][3], off_);
        }
        mask_[d] = 0;
    }
}

void SevenSeg::applyDigit(int d, uint8_t mask) {
    if (mask_[d] == mask) return;
    for (int s = 0; s < 7; ++s) {
        const bool on_now = mask & (1 << (6 - s));
        const bool on_before = mask_[d] & (1 << (6 - s));
        if (on_now != on_before) {
            lv_obj_set_style_bg_color(seg_[d][s], c(on_now ? on_ : off_), 0);
        }
    }
    mask_[d] = mask;
}

void SevenSeg::set(int32_t value) {
    if (value == last_) return;
    last_ = value;
    if (value < 0) {
        for (int d = 0; d < digits_; ++d) applyDigit(d, kDash);
        return;
    }
    int32_t v = value;
    for (int d = digits_ - 1; d >= 0; --d) {
        const int digit = v % 10;
        const bool leading = (v == 0 && d != digits_ - 1);
        applyDigit(d, leading ? 0 : kDigitMask[digit]);
        v /= 10;
    }
}

void SevenSeg::setOnColor(uint32_t color) {
    if (color == on_) return;
    on_ = color;
    for (int d = 0; d < digits_; ++d) {
        for (int s = 0; s < 7; ++s) {
            if (mask_[d] & (1 << (6 - s))) lv_obj_set_style_bg_color(seg_[d][s], c(on_), 0);
        }
    }
}

// ---------------------------------------------------------------- SegBar
void SegBar::create(lv_obj_t* parent, int32_t x, int32_t y, int32_t wdt, int32_t hgt, int segments,
                    int32_t gap, float yellow_from, float red_from) {
    n_ = segments > kMaxSeg ? kMaxSeg : segments;
    const int32_t sw = (wdt - (n_ - 1) * gap) / n_;
    const int32_t used = n_ * sw + (n_ - 1) * gap;
    const int32_t x0 = x + (wdt - used) / 2;
    for (int i = 0; i < n_; ++i) {
        const float f = static_cast<float>(i + 1) / n_;
        col_on_[i] = f > red_from ? kRed : (f > yellow_from ? kYellow : kGreen);
        // segmenti piu' alti verso la zona rossa (effetto crescente)
        const int32_t sh = hgt / 2 + static_cast<int32_t>((hgt / 2) * f);
        seg_[i] = box(parent, x0 + i * (sw + gap), y + (hgt - sh), sw, sh, kSegOff);
        shown_[i] = kSegOff;
    }
}

void SegBar::set(float fraction, bool flash, bool flash_phase) {
    const int lit = static_cast<int>(fraction * n_ + 0.5f);
    for (int i = 0; i < n_; ++i) {
        uint32_t want;
        if (flash) want = flash_phase ? kRed : kSegOff;
        else want = (i < lit) ? col_on_[i] : kSegOff;
        if (shown_[i] != want) {
            shown_[i] = want;
            lv_obj_set_style_bg_color(seg_[i], c(want), 0);
        }
    }
}

// ---------------------------------------------------------------- HBar
void HBar::create(lv_obj_t* parent, int32_t x, int32_t y, int32_t wdt, int32_t hgt,
                  float min_v, float max_v, uint32_t color) {
    min_ = min_v;
    max_ = max_v;
    x0_ = x;
    w_ = wdt;
    color_ = color;
    box(parent, x, y, wdt, hgt, kSegOff);  // traccia
    zero_px_ = (min_v < 0.0f && max_v > 0.0f) ? static_cast<int32_t>((-min_v) / (max_v - min_v) * wdt) : 0;
    if (zero_px_ > 0) box(parent, x + zero_px_ - 1, y - 4, 2, hgt + 8, kMuted);  // tacca dello zero
    fill_ = box(parent, x + zero_px_, y, 0, hgt, color);
    lv_obj_add_flag(fill_, LV_OBJ_FLAG_HIDDEN);
}

void HBar::set(float v, bool valid) {
    int32_t a = zero_px_, b = zero_px_;
    if (valid) {
        if (v < min_) v = min_;
        if (v > max_) v = max_;
        const int32_t p = static_cast<int32_t>((v - min_) / (max_ - min_) * w_);
        a = p < zero_px_ ? p : zero_px_;
        b = p < zero_px_ ? zero_px_ : p;
    }
    if (a == last_a_ && b == last_b_) return;
    last_a_ = a;
    last_b_ = b;
    if (b - a <= 0) {
        lv_obj_add_flag(fill_, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_remove_flag(fill_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_x(fill_, x0_ + a);
    lv_obj_set_width(fill_, b - a);
}

void HBar::setColor(uint32_t color) {
    if (color == color_) return;
    color_ = color;
    lv_obj_set_style_bg_color(fill_, c(color), 0);
}

// ---------------------------------------------------------------- Tile
void Tile::create(lv_obj_t* parent, int32_t x, int32_t y, int32_t wdt, int32_t hgt,
                  const char* title, const lv_font_t* value_font) {
    w_ = wdt;
    h_ = hgt;
    box_ = box(parent, x, y, wdt, hgt, kPanel);
    // filetto rosso a sinistra (piccola area statica)
    box(box_, 0, 0, 4, hgt, kRed);
    lv_obj_t* t = label(box_, title, fontSmall(), kMuted);
    lv_obj_set_pos(t, kPad + 4, kPad - 2);
    value_ = label(box_, "--", value_font, kText);
    lv_obj_align(value_, LV_ALIGN_LEFT_MID, kPad + 4, 8);
    unit_ = label(box_, "", fontSmall(), kMuted);
    lv_obj_align(unit_, LV_ALIGN_BOTTOM_RIGHT, -kPad, -kPad + 2);
}

void Tile::set(const char* value, const char* unit, uint32_t color) {
    setText(value_, value);
    setText(unit_, unit);
    setColor(value_, color, color_);
}

}  // namespace abarth::ui_v2::w
