// Pagina PERSONALE (layout v2): schede scelte dall'utente su una griglia di
// 4 colonne, disposte in ordine riempiendo gli spazi; la pagina scorre in
// verticale. Matita in alto a destra (o pressione lunga su una scheda) per
// modificare: tocca una scheda per sceglierla e usa il pannello (Grandezza,
// Tipo, Larghezza, Altezza, Sposta, Rimuovi); "+" aggiunge una scheda; la
// spunta salva in NVS. Le schede si ricostruiscono solo quando il layout cambia.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <new>
#include <esp_heap_caps.h>
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
using custom::kMaxCards;
using custom::WidgetType;

constexpr int32_t kHeadH = 64;     ///< riga in alto: suggerimento + matita
constexpr int32_t kSheetH = 3 * 64 + 2 * kGridGap + 2 * 20;   ///< pannello editor
constexpr int kAddIndex = 100;     ///< id della scheda "+" (aggiungi)

struct CardView {
    lv_obj_t*    card = nullptr;
    lv_obj_t*    value = nullptr;
    lv_obj_t*    unit = nullptr;
    lv_obj_t*    minmax = nullptr;
    const lv_font_t* vfont = nullptr;
    w::StatusDot dot;
    bool         has_dot = false;
    w::PillBar   bar;
    bool         has_bar = false;
    bool         show_unit = true;
    Binding      bind;
    PeakTracker  peak;
    uint32_t     value_col = 0;
    int32_t      value_x = 0, value_y = 0, inner_w = 0;
    char         last_minmax[64] = "";
};

lv_obj_t* g_page = nullptr;
lv_obj_t* g_head = nullptr;     ///< riga fissa in alto
lv_obj_t* g_grid = nullptr;     ///< contenitore scorrevole delle schede
lv_obj_t* g_sheet = nullptr;    ///< pannello dell'editor
lv_obj_t* g_picker = nullptr;   ///< elenco grandezze
lv_obj_t* g_pencil = nullptr;
lv_obj_t* g_sheet_btn[6] = {};
int32_t   g_w = 0, g_h = 0;
custom::Layout g_layout;
// In PSRAM (allocato in build): 32 schede in .data occupavano ~5 KB di RAM
// interna statica e non restava memoria per i task di sistema all'avvio.
CardView* g_views = nullptr;
lv_obj_t* g_card_obj[kMaxCards] = {};
bool      g_editing = false;
int       g_selected = -1;
bool      g_moving = false;
bool      g_adding = false;

int32_t col_w() { return (g_w - 2 * kMargin - 3 * kGridGap) / 4; }

void rect_of(const custom::Placement& p, int32_t& x, int32_t& y, int32_t& wd, int32_t& ht) {
    x = kMargin + p.col * (col_w() + kGridGap);
    y = 4 + p.row * (kGridRowH + kGridGap);
    wd = p.cols * col_w() + (p.cols - 1) * kGridGap;
    ht = p.rows * kGridRowH + (p.rows - 1) * kGridGap;
}

void rebuild();
void open_picker();

// --- interazione ---------------------------------------------------------
void enter_edit(int sel) {
    g_editing = true;
    g_moving = false;
    g_selected = g_layout.count > 0 ? (sel >= 0 && sel < g_layout.count ? sel : 0) : -1;
    g_layout.flags |= 1;   // suggerimento visto
}

void card_long_cb(lv_event_t* e) {
    if (g_editing) return;
    enter_edit(static_cast<int>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e))));
    rebuild();
}

void card_click_cb(lv_event_t* e) {
    if (!g_editing) return;
    const int s = static_cast<int>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
    if (s == kAddIndex) {   // "+": scegli la grandezza della nuova scheda
        g_adding = true;
        open_picker();
        return;
    }
    if (g_moving && g_selected >= 0 && s != g_selected) {
        if (custom::swap(g_layout, g_selected, s)) g_selected = s;
        g_moving = false;
    } else {
        g_selected = s;
        g_moving = false;
    }
    rebuild();
}

void pencil_cb(lv_event_t*) {
    if (g_picker) return;
    if (g_editing) {   // spunta: salva e chiude
        custom::save(g_layout);
        g_editing = false;
        g_selected = -1;
        g_moving = false;
    } else {
        enter_edit(0);
    }
    rebuild();
}

void picker_close() {
    if (g_picker) { lv_obj_delete_async(g_picker); g_picker = nullptr; }
}

void picker_pick_cb(lv_event_t* e) {
    const uint32_t v = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
    if (g_adding) {
        if (v != custom::kEmpty) {
            const int i = custom::add(g_layout, static_cast<uint8_t>(v), WidgetType::BigNumber, 2, 1);
            if (i >= 0) g_selected = i;
        }
        g_adding = false;
    } else if (g_selected >= 0) {
        if (v == custom::kEmpty) {
            custom::remove(g_layout, g_selected);
            g_selected = g_layout.count > 0 ? (g_selected < g_layout.count ? g_selected : g_layout.count - 1) : -1;
        } else {
            custom::setMetric(g_layout, g_selected, static_cast<uint8_t>(v));
        }
    }
    picker_close();
    rebuild();
}

void open_picker() {
    // Elenco a tutta pagina, righe semplici su una scheda
    g_picker = w::box(g_page, 0, 0, g_w, g_h, kBg);
    lv_obj_add_flag(g_picker, LV_OBJ_FLAG_CLICKABLE);   // non far passare i tocchi alle schede sotto
    lv_obj_remove_flag(g_picker, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_flag(g_picker, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(g_picker, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(g_picker, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(g_picker, LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_remove_flag(g_picker, LV_OBJ_FLAG_GESTURE_BUBBLE);
    size_t n = 0;
    const MetricInfo* cat = metricCatalog(n);
    const int32_t cw = g_w - 2 * kMargin;
    lv_obj_t* t = w::label(g_picker, g_adding ? "NUOVA SCHEDA" : "SCEGLI GRANDEZZA", fontLabel(), kDim, true);
    lv_obj_set_pos(t, kMargin + kCardPad, 14);
    const size_t rows = g_adding ? n + 1 : n + 1;
    lv_obj_t* card = w::card(g_picker, kMargin, 50, cw, static_cast<int32_t>(rows * (kRowH + 1)));
    for (size_t i = 0; i < rows; ++i) {
        const int32_t ry = static_cast<int32_t>(i) * (kRowH + 1);
        lv_obj_t* row = w::box(card, 0, ry, cw, kRowH, kCard);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_STATE_PRESSED);
        lv_obj_set_style_radius(row, kRadius, 0);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_color(row, c(kCardPress), LV_STATE_PRESSED);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_GESTURE_BUBBLE);
        const char* name = i < n ? cat[i].long_name : (g_adding ? "Annulla" : "Rimuovi questa scheda");
        lv_obj_t* l = w::label(row, name, fontBody(), i < n ? kText : kSecondary);
        lv_obj_align(l, LV_ALIGN_LEFT_MID, kCardPad, 0);
        const uintptr_t val = i < n ? static_cast<uintptr_t>(cat[i].metric) : custom::kEmpty;
        lv_obj_add_event_cb(row, picker_pick_cb, LV_EVENT_CLICKED, reinterpret_cast<void*>(val));
        if (i + 1 < rows) w::box(card, kCardPad, ry + kRowH, cw - 2 * kCardPad, 1, kRowLine);
    }
}

void action_cb(lv_event_t* e) {
    const int a = static_cast<int>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
    if (g_selected < 0 || g_selected >= g_layout.count) return;
    custom::Card& cd = g_layout.cards[g_selected];
    switch (a) {
        case 0: open_picker(); return;
        case 1: {   // tipo successivo tra quelli ammessi per questa dimensione
            for (int k = 1; k <= static_cast<int>(WidgetType::Count); ++k) {
                const auto t = static_cast<WidgetType>((cd.type + k) % static_cast<int>(WidgetType::Count));
                if (custom::setType(g_layout, g_selected, t)) break;
            }
            break;
        }
        case 2: custom::setSize(g_layout, g_selected, cd.cols == 1 ? 2 : cd.cols == 2 ? 4 : 1, cd.rows); break;
        case 3: custom::setSize(g_layout, g_selected, cd.cols, cd.rows == 1 ? 2 : 1); break;
        case 4: g_moving = !g_moving; break;
        case 5:
            custom::remove(g_layout, g_selected);
            g_selected = g_layout.count > 0 ? (g_selected < g_layout.count ? g_selected : g_layout.count - 1) : -1;
            break;
        default: break;
    }
    rebuild();
}

// --- costruzione -----------------------------------------------------------
void build_card(int i, const custom::Placement& pl) {
    CardView& v = g_views[i];
    v = CardView{};
    const custom::Card& cd = g_layout.cards[i];
    int32_t x, y, wd, ht;
    rect_of(pl, x, y, wd, ht);
    v.card = w::card(g_grid, x, y, wd, ht);
    g_card_obj[i] = v.card;
    lv_obj_add_flag(v.card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(v.card, card_long_cb, LV_EVENT_LONG_PRESSED, reinterpret_cast<void*>(static_cast<uintptr_t>(i)));
    lv_obj_add_event_cb(v.card, card_click_cb, LV_EVENT_CLICKED, reinterpret_cast<void*>(static_cast<uintptr_t>(i)));
    if (g_editing && i == g_selected) {
        lv_obj_set_style_border_width(v.card, 2, 0);
        lv_obj_set_style_border_color(v.card, c(g_moving ? kWarn : accent()), 0);
    }
    const auto m = static_cast<data::Metric>(cd.metric);
    const MetricInfo* mi = metricInfo(m);
    const bool tiny = cd.cols == 1;
    const bool low = cd.rows == 1;
    const int32_t pad = tiny ? 14 : (low ? 20 : kCardPad);
    v.inner_w = wd - 2 * pad;

    // etichetta (nelle schede da 1/4 senza spaziatura e con "..." se lunga)
    lv_obj_t* t = w::label(v.card, mi ? mi->name : "?", fontLabel(), kDim, !tiny);
    lv_obj_set_pos(t, pad, low ? 12 : pad);
    v.has_dot = hasAlertThreshold(m);
    if (tiny) {
        lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
        lv_obj_set_width(t, v.inner_w - (v.has_dot ? kStatusDot + 4 : 0));
    }
    if (v.has_dot) v.dot.create(v.card, wd - pad - kStatusDot, (low ? 12 : pad) + 3);

    // valore: 75 px nelle schede alte larghe, 48 px nelle basse, 32 px nelle 1/4
    v.vfont = tiny ? fontMedium() : (low ? fontValueSmall() : fontValue());
    const auto type = static_cast<WidgetType>(cd.type);
    int32_t bottom = ht - (low ? 12 : pad);
    if (type == WidgetType::NumberBar && !tiny) {
        v.bar.create(v.card, pad, bottom - kPillH, wd - 2 * pad);
        v.has_bar = true;
        bottom -= kPillH + (low ? 8 : 16);
    } else if (type == WidgetType::PeakMinMax && !tiny) {
        v.minmax = w::label(v.card, "", low ? fontLabel() : fontUnit(), kSecondary);
        if (low) {   // nelle basse: a destra, sulla riga del valore
            lv_obj_set_style_text_align(v.minmax, LV_TEXT_ALIGN_RIGHT, 0);
            lv_obj_set_width(v.minmax, wd / 2 - pad);
            lv_obj_set_pos(v.minmax, wd / 2, bottom - lv_font_get_line_height(fontLabel()));
        } else {
            lv_obj_set_pos(v.minmax, pad, bottom - lv_font_get_line_height(fontUnit()));
            bottom -= lv_font_get_line_height(fontUnit()) + 8;
        }
    }
    v.value_x = pad;
    v.value_y = bottom - lv_font_get_line_height(v.vfont);
    v.value = w::label(v.card, fmt::kNoData, v.vfont, kDim);
    lv_obj_set_pos(v.value, v.value_x, v.value_y);
    v.value_col = kDim;
    v.unit = w::label(v.card, "", tiny ? fontLabel() : fontUnit(), kDim);
    v.bind.bind(m);
}

void build_add_card(const custom::Placement& pl) {
    int32_t x, y, wd, ht;
    rect_of(pl, x, y, wd, ht);
    lv_obj_t* cardo = w::card(g_grid, x, y, wd, ht);
    lv_obj_add_flag(cardo, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(cardo, c(kCardPress), LV_STATE_PRESSED);
    lv_obj_add_event_cb(cardo, card_click_cb, LV_EVENT_CLICKED, reinterpret_cast<void*>(static_cast<uintptr_t>(kAddIndex)));
    lv_obj_t* plus = w::label(cardo, "+  Aggiungi", fontBody(), kSecondary);
    lv_obj_center(plus);
}

void build_head() {
    // Riga in alto (fissa): suggerimento e matita / spunta
    for (uint32_t i = lv_obj_get_child_count(g_head); i > 0; --i) lv_obj_delete(lv_obj_get_child(g_head, i - 1));
    const bool hint = !g_editing && !(g_layout.flags & 1);
    if (hint || g_editing) {
        const char* txt = g_editing ? (g_moving ? "Tocca dove spostarla" : "Tocca una scheda per modificarla")
                                    : "Tieni premuta una scheda o tocca la matita";
        lv_obj_t* l = w::label(g_head, txt, fontLabel(), kSecondary);
        lv_obj_set_pos(l, kMargin + 6, (kHeadH - lv_font_get_line_height(fontLabel())) / 2);
    }
    g_pencil = w::box(g_head, g_w - kMargin - 64, 0, 64, kHeadH, kBg, 16);
    lv_obj_add_flag(g_pencil, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(g_pencil, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_set_style_bg_color(g_pencil, c(kCardPress), LV_STATE_PRESSED);
    lv_obj_t* ic = w::label(g_pencil, g_editing ? LV_SYMBOL_OK : LV_SYMBOL_EDIT, &lv_font_montserrat_28,
                            g_editing ? accent() : kSecondary);
    lv_obj_center(ic);
    lv_obj_add_event_cb(g_pencil, pencil_cb, LV_EVENT_CLICKED, nullptr);
}

void build_sheet() {
    // Pannello editor fisso in basso; la griglia ha spazio extra per scorrere sopra
    const int32_t cw = g_w - 2 * kMargin;
    g_sheet = w::card(g_page, kMargin, g_h - kSheetH - 8, cw, kSheetH);
    lv_obj_set_style_border_width(g_sheet, 1, 0);
    lv_obj_set_style_border_color(g_sheet, c(kTrack), 0);
    lv_obj_add_flag(g_sheet, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(g_sheet, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_remove_flag(g_sheet, LV_OBJ_FLAG_GESTURE_BUBBLE);
    const custom::Card* cd = (g_selected >= 0 && g_selected < g_layout.count) ? &g_layout.cards[g_selected] : nullptr;
    const int32_t bw = (cw - 40 - kGridGap) / 2;
    for (int i = 0; i < 6; ++i) {
        char txt[40];
        static const char* const tn[] = {"Numero", "Barra", "Min/Max"};
        switch (i) {
            case 0: std::snprintf(txt, sizeof(txt), "Grandezza"); break;
            case 1: std::snprintf(txt, sizeof(txt), "Tipo: %s", cd ? tn[cd->type % 3] : "-"); break;
            case 2: std::snprintf(txt, sizeof(txt), "Larghezza: %s", !cd ? "-" : cd->cols == 1 ? "1/4" : cd->cols == 2 ? "1/2" : "intera"); break;
            case 3: std::snprintf(txt, sizeof(txt), "Altezza: %s", !cd ? "-" : cd->rows == 1 ? "bassa" : "alta"); break;
            case 4: std::snprintf(txt, sizeof(txt), "%s", g_moving ? "Annulla sposta" : "Sposta"); break;
            default: std::snprintf(txt, sizeof(txt), "Rimuovi"); break;
        }
        const int32_t bx = 20 + (i % 2) * (bw + kGridGap);
        const int32_t by = 20 + (i / 2) * (64 + kGridGap);
        lv_obj_t* b = w::box(g_sheet, bx, by, bw, 64, kTrack, 18);
        g_sheet_btn[i] = b;
        lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(b, LV_OBJ_FLAG_GESTURE_BUBBLE);
        lv_obj_set_style_bg_color(b, c(kCardPress), LV_STATE_PRESSED);
        lv_obj_t* l = w::label(b, txt, fontBody(), i == 5 ? kCrit : (cd ? kText : kDim));
        lv_obj_center(l);
        lv_obj_add_event_cb(b, action_cb, LV_EVENT_CLICKED, reinterpret_cast<void*>(static_cast<uintptr_t>(i)));
    }
}

void rebuild() {
    if (g_sheet) { lv_obj_delete(g_sheet); g_sheet = nullptr; }
    for (auto& o : g_sheet_btn) o = nullptr;
    const int32_t keep_scroll = lv_obj_get_scroll_y(g_grid);
    lv_obj_clean(g_grid);
    for (auto& o : g_card_obj) o = nullptr;
    build_head();

    custom::Placement pl[kMaxCards + 1];
    // nell'editor si aggiunge in coda una scheda "+" (1/2 bassa) per aggiungere
    custom::Layout tmp = g_layout;
    const bool can_add = g_editing && tmp.count < kMaxCards;
    if (can_add) custom::add(tmp, 0, WidgetType::BigNumber, 2, 1);
    const int rows = custom::pack(tmp, pl);
    for (int i = 0; i < g_layout.count; ++i) build_card(i, pl[i]);
    if (can_add) build_add_card(pl[g_layout.count]);
    // spazio in fondo: nell'editor tutte le schede devono poter salire sopra il pannello
    const int32_t content_h = 4 + rows * (kGridRowH + kGridGap) + (g_editing ? kSheetH + 16 : 8);
    w::box(g_grid, 0, content_h - 1, 1, 1, kBg);
    lv_obj_update_layout(g_grid);
    lv_obj_scroll_to_y(g_grid, keep_scroll, LV_ANIM_OFF);
    if (g_editing) {
        build_sheet();
        // la scheda scelta deve restare visibile sopra il pannello
        if (g_selected >= 0 && g_card_obj[g_selected]) {
            const int32_t top = lv_obj_get_y(g_card_obj[g_selected]) - lv_obj_get_scroll_y(g_grid);
            const int32_t bot = top + lv_obj_get_height(g_card_obj[g_selected]);
            const int32_t vis = lv_obj_get_height(g_grid) - kSheetH - 8;
            if (bot > vis) lv_obj_scroll_by(g_grid, 0, -(bot - vis + 8), LV_ANIM_OFF);
            else if (top < 0) lv_obj_scroll_by(g_grid, 0, -top + 4, LV_ANIM_OFF);
        }
    }
}
}  // namespace

bool editing() { return g_editing; }
int debugSlotType(int s) { return (s >= 0 && s < g_layout.count) ? g_layout.cards[s].type : -1; }
int debugCardCount() { return g_layout.count; }
lv_obj_t* debugPencil() { return g_pencil; }
lv_obj_t* debugSheetButton(int i) { return (i >= 0 && i < 6) ? g_sheet_btn[i] : nullptr; }
lv_obj_t* debugGrid() { return g_grid; }

void debugMode(int mode) {
    picker_close();
    g_editing = mode > 0;
    g_selected = mode > 0 && g_layout.count > 3 ? 3 : (mode > 0 ? 0 : -1);
    g_moving = false;
    g_adding = false;
    rebuild();
    if (mode == 2) open_picker();
}

void build(lv_obj_t* parent, int32_t W, int32_t H, int32_t /*y0*/) {
    g_page = parent;
    g_w = W;
    g_h = H;
    g_layout = custom::load();
    void* mem = heap_caps_calloc(kMaxCards, sizeof(CardView), MALLOC_CAP_SPIRAM);
    if (!mem) return;
    g_views = new (mem) CardView[kMaxCards];
    g_head = w::box(parent, 0, 0, W, kHeadH, kBg);
    lv_obj_set_style_bg_opa(g_head, LV_OPA_TRANSP, 0);
    // contenitore scorrevole in verticale (lo swipe orizzontale resta per cambiare pagina)
    g_grid = w::box(parent, 0, kHeadH, W, H - kHeadH, kBg);
    lv_obj_set_style_bg_opa(g_grid, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(g_grid, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(g_grid, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(g_grid, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(g_grid, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(g_grid, LV_OBJ_FLAG_SCROLL_ELASTIC);
    rebuild();
}

void update(int64_t now_ms) {
    if (g_picker || !g_views) return;
    for (int s = 0; s < g_layout.count; ++s) {
        CardView& v = g_views[s];
        if (!v.value) continue;
        const bool changed = v.bind.update(now_ms);
        const bool missing = v.bind.level == Level::Missing;
        if (changed) {
            const char* txt = missing ? fmt::kNoData : v.bind.text;
            w::setText(v.value, txt);
            w::setColor(v.value, missing ? kDim : kText, v.value_col);
            // unita' dopo il valore, solo se ci sta (schede da 1/4)
            const lv_font_t* uf = lv_obj_get_style_text_font(v.unit, LV_PART_MAIN);
            const int32_t vw = w::textWidth(txt, v.vfont);
            const bool fits = vw + 6 + w::textWidth(v.bind.unit, uf) <= v.inner_w;
            lv_label_set_text(v.unit, fits ? v.bind.unit : "");
            const int32_t base = v.value_y + lv_font_get_line_height(v.vfont) - v.vfont->base_line;
            lv_obj_set_pos(v.unit, v.value_x + vw + 6, base - (lv_font_get_line_height(uf) - uf->base_line));
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
