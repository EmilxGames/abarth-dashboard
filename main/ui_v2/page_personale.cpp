// Pagina PERSONALE: schede scelte dall'utente. Tieni premuto una scheda per
// entrare in modifica; tocca una scheda per selezionarla (contorno accento) e
// usa il pannello: Grandezza, Tipo, Larghezza, Sposta, Rimuovi. "Fatto" salva
// in NVS. Le schede vengono ricostruite solo quando il layout cambia.
#include <cmath>
#include <cstdio>
#include <cstring>
#include "lvgl.h"
#include "ui_v2/alert_levels.h"
#include "ui_v2/binding.h"
#include "ui_v2/custom_layout.h"
#include "ui_v2/metric_info.h"
#include "ui_v2/page_personale.h"
#include "ui_v2/theme.h"
#include "ui_v2/widgets.h"

namespace abarth::ui_v2::personale {

namespace {
using namespace theme;
using custom::kSlots;
using custom::WidgetType;

struct SlotView {
    lv_obj_t*    card = nullptr;
    lv_obj_t*    value = nullptr;
    lv_obj_t*    unit = nullptr;
    lv_obj_t*    minmax = nullptr;
    w::StatusDot dot;
    bool         has_dot = false;
    w::PillBar   bar;
    bool         has_bar = false;
    Binding      bind;
    PeakTracker  peak;
    uint32_t     value_col = 0;
    int32_t      value_y = 0;
    char         last_minmax[64] = "";
};

lv_obj_t* g_page = nullptr;
lv_obj_t* g_grid = nullptr;     ///< contenitore delle schede (ricostruito al cambio layout)
lv_obj_t* g_sheet = nullptr;    ///< pannello dell'editor
lv_obj_t* g_picker = nullptr;   ///< elenco grandezze
lv_obj_t* g_sheet_info = nullptr;
int32_t   g_w = 0, g_h = 0, g_y0 = 0;
custom::Layout g_layout;
SlotView  g_views[kSlots];
bool      g_editing = false;
int       g_selected = -1;
bool      g_moving = false;

int32_t row_h() { return (kCardsBot - kCardsTop - 3 * kGap) / 4; }
int32_t half_w() { return (g_w - 2 * kMargin - kGap) / 2; }

void slot_rect(int s, int32_t& x, int32_t& y, int32_t& wd, int32_t& ht) {
    const int r = s / custom::kCols, c = s % custom::kCols;
    const int32_t cw = g_w - 2 * kMargin;
    x = kMargin + (c ? half_w() + kGap : 0);
    y = kCardsTop - g_y0 + r * (row_h() + kGap);
    wd = g_layout.slots[s].span == 2 ? cw : (c ? cw - half_w() - kGap : half_w());
    ht = row_h();
}

void rebuild();
void show_sheet();

// --- interazione ---------------------------------------------------------
void card_long_cb(lv_event_t* e) {
    if (g_editing) return;
    g_editing = true;
    g_selected = static_cast<int>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
    rebuild();
}

void card_click_cb(lv_event_t* e) {
    if (!g_editing) return;
    const int s = static_cast<int>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
    if (g_moving && g_selected >= 0 && s != g_selected) {
        if (custom::swap(g_layout, g_selected, s)) g_selected = s;
        g_moving = false;
    } else {
        g_selected = s;
        g_moving = false;
    }
    rebuild();
}

void picker_pick_cb(lv_event_t* e) {
    const uint32_t v = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
    if (g_selected >= 0) custom::setMetric(g_layout, g_selected, static_cast<uint8_t>(v));
    if (g_picker) { lv_obj_delete(g_picker); g_picker = nullptr; }
    rebuild();
}

void open_picker() {
    // Elenco a tutta pagina, righe semplici su schede (una sola volta alla volta)
    g_picker = w::box(g_page, 0, 0, g_w, g_h, kBg);
    lv_obj_add_flag(g_picker, LV_OBJ_FLAG_CLICKABLE);   // non far passare i tocchi alle schede sotto
    lv_obj_remove_flag(g_picker, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_flag(g_picker, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(g_picker, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(g_picker, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(g_picker, LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_remove_flag(g_picker, LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_remove_flag(g_picker, LV_OBJ_FLAG_GESTURE_BUBBLE);
    size_t n = 0;
    const MetricInfo* cat = metricCatalog(n);
    const int32_t cw = g_w - 2 * kMargin;
    lv_obj_t* t = w::label(g_picker, "SCEGLI GRANDEZZA", fontLabel(), kDim, true);
    lv_obj_set_pos(t, kMargin + kCardPad, 14);
    lv_obj_t* card = w::card(g_picker, kMargin, 50, cw, static_cast<int32_t>((n + 1) * (kRowH + 1)));
    for (size_t i = 0; i <= n; ++i) {
        const int32_t ry = static_cast<int32_t>(i) * (kRowH + 1);
        lv_obj_t* row = w::box(card, 0, ry, cw, kRowH, kCard);
        if (i == 0 || i == n) lv_obj_set_style_radius(row, kRadius, 0);  // segue gli angoli della scheda
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_STATE_PRESSED);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_color(row, c(kCardPress), LV_STATE_PRESSED);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_GESTURE_BUBBLE);
        const char* name = i < n ? cat[i].long_name : "Nessuna (scheda vuota)";
        lv_obj_t* l = w::label(row, name, fontBody(), i < n ? kText : kSecondary);
        lv_obj_align(l, LV_ALIGN_LEFT_MID, kCardPad, 0);
        const uintptr_t val = i < n ? static_cast<uintptr_t>(cat[i].metric) : custom::kEmpty;
        lv_obj_add_event_cb(row, picker_pick_cb, LV_EVENT_CLICKED, reinterpret_cast<void*>(val));
        if (i < n) w::box(card, kCardPad, ry + kRowH, cw - 2 * kCardPad, 1, kRowLine);
    }
    lv_obj_set_height(card, static_cast<int32_t>((n + 1) * (kRowH + 1)));
}

void action_cb(lv_event_t* e) {
    const int a = static_cast<int>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
    if (a == 5) {  // Fatto
        custom::save(g_layout);
        g_editing = false;
        g_selected = -1;
        g_moving = false;
        rebuild();
        return;
    }
    if (g_selected < 0) return;
    custom::Slot& sl = g_layout.slots[g_selected];
    switch (a) {
        case 0: open_picker(); return;
        case 1: custom::setType(g_layout, g_selected, static_cast<WidgetType>((sl.type + 1) % static_cast<int>(WidgetType::Count))); break;
        case 2: custom::setSpan(g_layout, g_selected, sl.span == 2 ? 1 : 2); break;
        case 3: g_moving = !g_moving; break;
        case 4: custom::setMetric(g_layout, g_selected, custom::kEmpty); break;
        default: break;
    }
    rebuild();
}

// --- costruzione -----------------------------------------------------------
void build_slot(int s) {
    SlotView& v = g_views[s];
    v = SlotView{};
    if (custom::isCovered(g_layout, s)) return;
    const custom::Slot& sl = g_layout.slots[s];
    int32_t x, y, wd, ht;
    slot_rect(s, x, y, wd, ht);
    const bool empty = sl.metric == custom::kEmpty;
    if (empty && !g_editing) return;   // fuori dall'editor gli slot vuoti non si vedono

    v.card = w::card(g_grid, x, y, wd, ht);
    lv_obj_add_flag(v.card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(v.card, card_long_cb, LV_EVENT_LONG_PRESSED, reinterpret_cast<void*>(static_cast<uintptr_t>(s)));
    lv_obj_add_event_cb(v.card, card_click_cb, LV_EVENT_CLICKED, reinterpret_cast<void*>(static_cast<uintptr_t>(s)));
    if (g_editing && s == g_selected) {
        lv_obj_set_style_border_width(v.card, 2, 0);
        lv_obj_set_style_border_color(v.card, c(g_moving ? kWarn : accent()), 0);
    }
    if (empty) {
        lv_obj_t* plus = w::label(v.card, "+", fontMedium(), kDim);
        lv_obj_center(plus);
        return;
    }
    const auto m = static_cast<data::Metric>(sl.metric);
    const MetricInfo* mi = metricInfo(m);
    lv_obj_t* t = w::label(v.card, mi ? mi->name : "?", fontLabel(), kDim, true);
    lv_obj_set_pos(t, kCardPad, kCardPad);
    v.has_dot = hasAlertThreshold(m);
    if (v.has_dot) v.dot.create(v.card, wd - kCardPad - kStatusDot, kCardPad + 3);
    const auto type = static_cast<WidgetType>(sl.type);
    int32_t bottom = ht - kCardPad;
    if (type == WidgetType::NumberBar) {
        v.bar.create(v.card, kCardPad, bottom - kPillH, wd - 2 * kCardPad);
        v.has_bar = true;
        bottom -= kPillH + 16;
    } else if (type == WidgetType::PeakMinMax) {
        v.minmax = w::label(v.card, "", fontUnit(), kSecondary);
        lv_obj_set_pos(v.minmax, kCardPad, bottom - lv_font_get_line_height(fontUnit()));
        bottom -= lv_font_get_line_height(fontUnit()) + 8;
    }
    v.value_y = bottom - lv_font_get_line_height(fontValue());
    v.value = w::label(v.card, fmt::kNoData, fontValue(), kDim);
    lv_obj_set_pos(v.value, kCardPad, v.value_y);
    v.value_col = kDim;
    v.unit = w::label(v.card, "", fontUnit(), kDim);
    v.bind.bind(m);
}

void build_sheet() {
    // Pannello editor: in basso, oppure in alto se la scheda scelta e' in basso
    const int32_t cw = g_w - 2 * kMargin;
    const int32_t sh = 3 * kRowH + 2 * kGap + 2 * kCardPad;
    const bool top = g_selected >= 4;
    const int32_t sy = top ? kCardsTop - g_y0 : kCardsBot - g_y0 - sh;
    g_sheet = w::card(g_page, kMargin, sy, cw, sh);
    lv_obj_set_style_border_width(g_sheet, 1, 0);
    lv_obj_set_style_border_color(g_sheet, c(kTrack), 0);
    static const char* const names[] = {"Grandezza", "Tipo", "Larghezza", "Sposta", "Rimuovi", "Fatto"};
    const int32_t bw = (cw - 2 * kCardPad - kGap) / 2;
    for (int i = 0; i < 6; ++i) {
        const int32_t bx = kCardPad + (i % 2) * (bw + kGap);
        const int32_t by = kCardPad + (i / 2) * (kRowH + kGap);
        const bool done = i == 5;
        lv_obj_t* b = w::box(g_sheet, bx, by, bw, kRowH, done ? accent() : kTrack, 18);
        lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_color(b, c(kCardPress), LV_STATE_PRESSED);
        lv_obj_remove_flag(b, LV_OBJ_FLAG_GESTURE_BUBBLE);
        char txt[32];
        const custom::Slot* sl = g_selected >= 0 ? &g_layout.slots[g_selected] : nullptr;
        if (i == 1 && sl) {
            static const char* const tn[] = {"Numero", "Barra", "Min/Max"};
            std::snprintf(txt, sizeof(txt), "Tipo: %s", tn[sl->type % 3]);
        } else if (i == 2 && sl) {
            std::snprintf(txt, sizeof(txt), "Larghezza: %s", sl->span == 2 ? "2x" : "1x");
        } else if (i == 3 && g_moving) {
            std::snprintf(txt, sizeof(txt), "Tocca la destinazione");
        } else {
            std::snprintf(txt, sizeof(txt), "%s", names[i]);
        }
        lv_obj_t* l = w::label(b, txt, fontBody(), done ? kBg : kText);
        lv_obj_center(l);
        lv_obj_add_event_cb(b, action_cb, LV_EVENT_CLICKED, reinterpret_cast<void*>(static_cast<uintptr_t>(i)));
    }
}

void rebuild() {
    if (g_sheet) { lv_obj_delete(g_sheet); g_sheet = nullptr; }
    lv_obj_clean(g_grid);
    for (int s = 0; s < kSlots; ++s) build_slot(s);
    if (g_editing) build_sheet();
}
}  // namespace

bool editing() { return g_editing; }

void debugMode(int mode) {
    if (g_picker) { lv_obj_delete(g_picker); g_picker = nullptr; }
    g_editing = mode > 0;
    g_selected = mode > 0 ? 3 : -1;
    g_moving = false;
    rebuild();
    if (mode == 2) open_picker();
}

void build(lv_obj_t* parent, int32_t W, int32_t H, int32_t y0) {
    g_page = parent;
    g_w = W;
    g_h = H;
    g_y0 = y0;
    g_layout = custom::load();
    g_grid = w::box(parent, 0, 0, W, H, kBg);
    lv_obj_set_style_bg_opa(g_grid, LV_OPA_TRANSP, 0);
    rebuild();
}

void update(int64_t now_ms) {
    if (g_picker) return;
    for (int s = 0; s < kSlots; ++s) {
        SlotView& v = g_views[s];
        if (!v.value) continue;
        const bool changed = v.bind.update(now_ms);
        const bool missing = v.bind.level == Level::Missing;
        if (changed) {
            w::setText(v.value, missing ? fmt::kNoData : v.bind.text);
            w::setColor(v.value, missing ? kDim : kText, v.value_col);
            lv_label_set_text(v.unit, v.bind.unit);
            const int32_t base = v.value_y + lv_font_get_line_height(fontValue()) - fontValue()->base_line;
            lv_obj_set_pos(v.unit, kCardPad + w::textWidth(missing ? fmt::kNoData : v.bind.text, fontValue()) + 8,
                           base - (lv_font_get_line_height(fontUnit()) - fontUnit()->base_line));
        }
        const Level dl = missing ? Level::Missing : alertLevel(v.bind.info->metric, v.bind.value);
        if (v.has_dot) v.dot.set(!missing, w::levelColor(dl));
        if (v.has_bar) v.bar.set(missing ? NAN : v.bind.fraction(), dl == Level::Warn ? kCrit : accent());
        if (v.minmax) {
            v.peak.feed(v.bind);
            char buf[64] = "";
            if (!std::isnan(v.peak.min_v)) {
                char a[16], b[16];
                const char* u = "";
                fmt::format(v.bind.info->kind, v.peak.min_v, v.bind.info->decimals, v.bind.info->unit, a, sizeof(a), u);
                fmt::format(v.bind.info->kind, v.peak.max_v, v.bind.info->decimals, v.bind.info->unit, b, sizeof(b), u);
                std::snprintf(buf, sizeof(buf), "MIN %s   MAX %s", a, b);
            }
            if (std::strcmp(buf, v.last_minmax) != 0) {
                std::snprintf(v.last_minmax, sizeof(v.last_minmax), "%s", buf);
                lv_label_set_text(v.minmax, buf);
            }
        }
    }
}

}  // namespace abarth::ui_v2::personale
