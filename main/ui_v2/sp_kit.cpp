#include "ui_v2/sp_kit.h"

#include <cstring>
#include <memory>
#include "lvgl.h"
#include "ui_v2/theme.h"
#include "ui_v2/widgets.h"

namespace abarth::ui_v2::sp {

namespace {
using namespace theme;

constexpr int32_t kHeaderH = 64;

struct Row {
    lv_obj_t* value = nullptr;      ///< label valore (o testo info)
    lv_obj_t* sw = nullptr;         ///< interruttore
    TextFn    text;
    std::string last;
    Fn        tap, minus, plus;
    std::function<bool()> get_b;
    std::function<void(bool)> set_b;
    std::function<void(int)> preview, commit;
    lv_obj_t* slider_val = nullptr;
};

struct Page {
    lv_obj_t* root = nullptr;
    lv_obj_t* list = nullptr;
    lv_obj_t* card = nullptr;
    int32_t   y = 8;          ///< prossima posizione libera nella lista
    int32_t   card_h = 0;
    int       rows_in_card = 0;
    std::vector<std::unique_ptr<Row>> rows;
    std::vector<std::function<void(int)>> choice_set;   // per le pagine di scelta
};

lv_obj_t* g_host = nullptr;
int32_t g_w = 0, g_h = 0;
std::vector<Page*> g_stack;
lv_obj_t* g_dialog = nullptr;

int32_t cw() { return g_w - 2 * kMargin; }

Page* page_of(lv_obj_t* list) {
    for (auto* p : g_stack) if (p->list == list) return p;
    return nullptr;
}

void close_card(Page* p) {
    if (p->card) {
        p->y += p->card_h + kGap + 6;
        p->card = nullptr;
        p->card_h = 0;
        p->rows_in_card = 0;
    }
}

/// Crea una riga di altezza h nella scheda corrente (aprendola se serve)
lv_obj_t* add_row(Page* p, int32_t h, bool clickable) {
    if (!p->card) {
        p->card = w::card(p->list, kMargin, p->y, cw(), 1);
        p->card_h = 0;
    }
    if (p->rows_in_card > 0) {
        w::box(p->card, kCardPad, p->card_h, cw() - 2 * kCardPad, 1, kRowLine);
        p->card_h += 1;
    }
    lv_obj_t* row = w::box(p->card, 0, p->card_h, cw(), h, kCard);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);   // il colore e' quello della scheda
    lv_obj_set_style_radius(row, kRadius, 0);
    if (clickable) {
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_color(row, c(kCardPress), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_STATE_PRESSED);
    }
    p->card_h += h;
    p->rows_in_card++;
    lv_obj_set_height(p->card, p->card_h);
    return row;
}

lv_obj_t* row_title(lv_obj_t* row, const char* title, int32_t h, uint32_t color = kText) {
    lv_obj_t* l = w::label(row, title, fontBody(), color);
    lv_obj_set_pos(l, kCardPad, (h - lv_font_get_line_height(fontBody())) / 2);
    return l;
}

lv_obj_t* row_value(lv_obj_t* row, int32_t h, int32_t right_inset, int32_t width) {
    lv_obj_t* v = w::label(row, "", fontBody(), kSecondary);
    lv_obj_set_style_text_align(v, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_long_mode(v, LV_LABEL_LONG_DOT);
    lv_obj_set_width(v, width);
    lv_obj_set_height(v, lv_font_get_line_height(fontBody()));  // una sola riga, "..." se lungo
    lv_obj_set_pos(v, cw() - right_inset - width, (h - lv_font_get_line_height(fontBody())) / 2);
    return v;
}

void row_click_cb(lv_event_t* e) {
    auto* r = static_cast<Row*>(lv_event_get_user_data(e));
    if (r->tap) {
        Fn f = r->tap;   // la callback puo' chiudere la pagina (e distruggere r)
        f();
    }
}

void back_cb(lv_event_t*) { pop(); }

void style_switch(lv_obj_t* s) {
    lv_obj_set_style_bg_color(s, c(kTrack), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s, c(kTrack), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s, c(accent()), static_cast<lv_style_selector_t>(LV_PART_INDICATOR | LV_STATE_CHECKED));
    lv_obj_set_style_bg_color(s, c(kText), LV_PART_KNOB);
    lv_obj_set_style_shadow_width(s, 0, LV_PART_KNOB);
    lv_obj_set_style_border_width(s, 0, LV_PART_MAIN);
}

lv_obj_t* small_btn(lv_obj_t* parent, int32_t x, int32_t y, const char* txt) {
    lv_obj_t* b = w::box(parent, x, y, 56, 48, kTrack, 14);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(b, c(kCardPress), LV_STATE_PRESSED);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_t* l = w::label(b, txt, fontBody(), kText);
    lv_obj_center(l);
    return b;
}
}  // namespace

void init(lv_obj_t* host, int32_t W, int32_t H) {
    g_host = host;
    g_w = W;
    g_h = H;
}

bool g_has_root = false;

int depth() { return static_cast<int>(g_stack.size()) - (g_has_root ? 1 : 0); }

lv_obj_t* pushRoot() {
    auto* p = new Page();
    p->root = w::box(g_host, 0, 0, g_w, g_h, kBg);
    lv_obj_set_style_bg_opa(p->root, LV_OPA_TRANSP, 0);
    p->list = w::box(p->root, 0, 0, g_w, g_h, kBg);
    lv_obj_set_style_bg_opa(p->list, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(p->list, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(p->list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(p->list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(p->list, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(p->list, LV_OBJ_FLAG_SCROLL_ELASTIC);
    g_stack.push_back(p);
    g_has_root = true;
    return p->list;
}

lv_obj_t* push(const char* title) {
    auto* p = new Page();
    p->root = w::box(g_host, 0, 0, g_w, g_h, kBg);
    lv_obj_remove_flag(p->root, LV_OBJ_FLAG_GESTURE_BUBBLE);
    // La sottopagina deve "prendere" i tocchi: senza CLICKABLE LVGL cerca un
    // oggetto toccabile sotto (l'elenco categorie) e scroll/tap finiscono li'.
    lv_obj_add_flag(p->root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(p->root, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_t* hdr = w::box(p->root, 0, 0, g_w, kHeaderH, kBg);
    lv_obj_add_flag(hdr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(hdr, back_cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* arrow = w::label(hdr, LV_SYMBOL_LEFT, fontBody(), kText);
    lv_obj_set_pos(arrow, kMargin + 6, (kHeaderH - lv_font_get_line_height(fontBody())) / 2);
    lv_obj_t* t = w::label(hdr, title, fontBody(), kText);
    lv_obj_set_pos(t, kMargin + 44, (kHeaderH - lv_font_get_line_height(fontBody())) / 2);
    p->list = w::box(p->root, 0, kHeaderH, g_w, g_h - kHeaderH, kBg);
    lv_obj_set_style_bg_opa(p->list, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(p->list, LV_OBJ_FLAG_CLICKABLE);   // si scorre anche toccando gli spazi vuoti
    lv_obj_add_flag(p->list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(p->list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(p->list, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(p->list, LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_remove_flag(p->list, LV_OBJ_FLAG_GESTURE_BUBBLE);
    // la pagina sotto viene nascosta: niente tocchi ne' disegno inutile
    if (!g_stack.empty()) lv_obj_add_flag(g_stack.back()->root, LV_OBJ_FLAG_HIDDEN);
    g_stack.push_back(p);
    return p->list;
}

void pop() {
    if (g_dialog) { lv_obj_delete(g_dialog); g_dialog = nullptr; }
    if (g_stack.empty() || (g_has_root && g_stack.size() == 1)) return;  // la radice resta
    Page* p = g_stack.back();
    g_stack.pop_back();
    lv_obj_add_flag(p->root, LV_OBJ_FLAG_HIDDEN);
    lv_obj_delete_async(p->root);   // asincrono: si puo' chiamare da una callback della pagina
    delete p;
    if (!g_stack.empty()) lv_obj_remove_flag(g_stack.back()->root, LV_OBJ_FLAG_HIDDEN);
}

void popAll() { while (depth() > 0) pop(); }

void section(lv_obj_t* list, const char* title) {
    Page* p = page_of(list);
    if (!p) return;
    close_card(p);
    lv_obj_t* l = w::label(list, title, fontLabel(), kDim, true);
    lv_obj_set_pos(l, kMargin + kCardPad, p->y);
    p->y += 34;
}

void rowNav(lv_obj_t* list, const char* title, TextFn value, Fn on_tap) {
    Page* p = page_of(list);
    if (!p) return;
    auto r = std::make_unique<Row>();
    lv_obj_t* row = add_row(p, kRowH, true);
    row_title(row, title, kRowH);
    lv_obj_t* chev = w::label(row, LV_SYMBOL_RIGHT, fontLabel(), kDim);
    lv_obj_set_pos(chev, cw() - kCardPad - 14, (kRowH - lv_font_get_line_height(fontLabel())) / 2);
    if (value) { r->value = row_value(row, kRowH, kCardPad + 30, cw() / 2 - 40); r->text = value; }
    r->tap = std::move(on_tap);
    lv_obj_add_event_cb(row, row_click_cb, LV_EVENT_CLICKED, r.get());
    p->rows.push_back(std::move(r));
}

void rowValue(lv_obj_t* list, const char* title, TextFn value, Fn on_tap) {
    Page* p = page_of(list);
    if (!p) return;
    auto r = std::make_unique<Row>();
    lv_obj_t* row = add_row(p, kRowH, static_cast<bool>(on_tap));
    row_title(row, title, kRowH);
    r->value = row_value(row, kRowH, kCardPad, cw() / 2 - 20);
    r->text = std::move(value);
    r->tap = std::move(on_tap);
    if (r->tap) lv_obj_add_event_cb(row, row_click_cb, LV_EVENT_CLICKED, r.get());
    p->rows.push_back(std::move(r));
}

void rowToggle(lv_obj_t* list, const char* title, std::function<bool()> get, std::function<void(bool)> set) {
    Page* p = page_of(list);
    if (!p) return;
    auto r = std::make_unique<Row>();
    lv_obj_t* row = add_row(p, kRowH, false);
    row_title(row, title, kRowH);
    r->sw = lv_switch_create(row);
    lv_obj_set_size(r->sw, 64, 36);
    lv_obj_set_pos(r->sw, cw() - kCardPad - 64, (kRowH - 36) / 2);
    style_switch(r->sw);
    lv_obj_remove_flag(r->sw, LV_OBJ_FLAG_GESTURE_BUBBLE);
    if (get()) lv_obj_add_state(r->sw, LV_STATE_CHECKED);
    r->get_b = std::move(get);
    r->set_b = std::move(set);
    lv_obj_add_event_cb(r->sw, [](lv_event_t* e) {
        auto* rr = static_cast<Row*>(lv_event_get_user_data(e));
        const bool on = lv_obj_has_state(rr->sw, LV_STATE_CHECKED);
        if (rr->set_b) rr->set_b(on);
    }, LV_EVENT_VALUE_CHANGED, r.get());
    p->rows.push_back(std::move(r));
}

void rowStepper(lv_obj_t* list, const char* title, TextFn value, Fn minus, Fn plus) {
    Page* p = page_of(list);
    if (!p) return;
    auto r = std::make_unique<Row>();
    lv_obj_t* row = add_row(p, kRowH, false);
    row_title(row, title, kRowH);
    const int32_t bx = cw() - kCardPad - 56;
    lv_obj_t* bp = small_btn(row, bx, (kRowH - 48) / 2, "+");
    lv_obj_t* bm = small_btn(row, bx - 64, (kRowH - 48) / 2, "\xE2\x88\x92");
    r->value = row_value(row, kRowH, kCardPad + 56 + 64 + 12, 150);
    r->text = std::move(value);
    r->minus = std::move(minus);
    r->plus = std::move(plus);
    lv_obj_add_event_cb(bm, [](lv_event_t* e) { auto* rr = static_cast<Row*>(lv_event_get_user_data(e)); if (rr->minus) rr->minus(); refresh(); }, LV_EVENT_CLICKED, r.get());
    lv_obj_add_event_cb(bp, [](lv_event_t* e) { auto* rr = static_cast<Row*>(lv_event_get_user_data(e)); if (rr->plus) rr->plus(); refresh(); }, LV_EVENT_CLICKED, r.get());
    p->rows.push_back(std::move(r));
}

void rowAction(lv_obj_t* list, const char* title, Fn on_tap, bool destructive) {
    Page* p = page_of(list);
    if (!p) return;
    auto r = std::make_unique<Row>();
    lv_obj_t* row = add_row(p, kRowH, true);
    row_title(row, title, kRowH, destructive ? kCrit : kText);
    r->tap = std::move(on_tap);
    lv_obj_add_event_cb(row, row_click_cb, LV_EVENT_CLICKED, r.get());
    p->rows.push_back(std::move(r));
}

lv_obj_t* rowCustom(lv_obj_t* list, int32_t h) {
    Page* p = page_of(list);
    return p ? add_row(p, h, false) : nullptr;
}

int32_t rowWidth() { return cw(); }

void rowInfo(lv_obj_t* list, TextFn text) {
    Page* p = page_of(list);
    if (!p) return;
    auto r = std::make_unique<Row>();
    const std::string first = text ? text() : std::string();
    // altezza calcolata sul testo iniziale (righe fisse, niente layout dinamico)
    lv_point_t sz;
    lv_text_get_size(&sz, first.c_str(), fontUnit(), 0, 4, cw() - 2 * kCardPad, LV_TEXT_FLAG_NONE);
    const int32_t h = sz.y + 2 * 20;
    lv_obj_t* row = add_row(p, h, false);
    r->value = w::label(row, first.c_str(), fontUnit(), kSecondary);
    lv_obj_set_style_text_line_space(r->value, 4, 0);
    lv_label_set_long_mode(r->value, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(r->value, cw() - 2 * kCardPad);
    lv_obj_set_pos(r->value, kCardPad, 20);
    r->text = std::move(text);
    r->last = first;
    p->rows.push_back(std::move(r));
}

void rowSlider(lv_obj_t* list, const char* title, int min_v, int max_v, std::function<int()> get,
               std::function<void(int)> preview, std::function<void(int)> commit) {
    Page* p = page_of(list);
    if (!p) return;
    auto r = std::make_unique<Row>();
    const int32_t h = kRowH + 56;
    lv_obj_t* row = add_row(p, h, false);
    row_title(row, title, kRowH);
    r->slider_val = row_value(row, kRowH, kCardPad, 140);
    lv_obj_t* sl = lv_slider_create(row);
    lv_obj_remove_style_all(sl);
    lv_obj_set_pos(sl, kCardPad + 14, kRowH + 8);
    lv_obj_set_size(sl, cw() - 2 * kCardPad - 28, kPillH);
    lv_obj_set_style_bg_color(sl, c(kTrack), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(sl, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(sl, kPillH, LV_PART_MAIN);
    lv_obj_set_style_bg_color(sl, c(accent()), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(sl, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(sl, kPillH, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(sl, c(kText), LV_PART_KNOB);
    lv_obj_set_style_bg_opa(sl, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_radius(sl, LV_RADIUS_CIRCLE, LV_PART_KNOB);
    lv_obj_set_style_pad_all(sl, 8, LV_PART_KNOB);
    lv_obj_set_ext_click_area(sl, 20);
    lv_obj_remove_flag(sl, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_slider_set_range(sl, min_v, max_v);
    lv_slider_set_value(sl, get(), LV_ANIM_OFF);
    lv_label_set_text_fmt(r->slider_val, "%d %%", get());
    r->preview = std::move(preview);
    r->commit = std::move(commit);
    auto cb = [](lv_event_t* e) {
        auto* rr = static_cast<Row*>(lv_event_get_user_data(e));
        lv_obj_t* s = static_cast<lv_obj_t*>(lv_event_get_target(e));
        const int v = lv_slider_get_value(s);
        if (lv_event_get_code(e) == LV_EVENT_RELEASED) { if (rr->commit) rr->commit(v); }
        else if (rr->preview) rr->preview(v);
        lv_label_set_text_fmt(rr->slider_val, "%d %%", v);
    };
    lv_obj_add_event_cb(sl, cb, LV_EVENT_VALUE_CHANGED, r.get());
    lv_obj_add_event_cb(sl, cb, LV_EVENT_RELEASED, r.get());
    p->rows.push_back(std::move(r));
}

void choice(const char* title, const std::vector<std::string>& options, std::function<int()> get,
            std::function<void(int)> set) {
    lv_obj_t* list = push(title);
    Page* p = page_of(list);
    const int cur = get ? get() : -1;
    auto setter = std::make_shared<std::function<void(int)>>(std::move(set));
    for (size_t i = 0; i < options.size(); ++i) {
        auto r = std::make_unique<Row>();
        lv_obj_t* row = add_row(p, kRowH, true);
        row_title(row, options[i].c_str(), kRowH);
        if (static_cast<int>(i) == cur) {
            lv_obj_t* ok = w::label(row, LV_SYMBOL_OK, fontBody(), accent());
            lv_obj_set_pos(ok, cw() - kCardPad - 28, (kRowH - lv_font_get_line_height(fontBody())) / 2);
        }
        const int idx = static_cast<int>(i);
        r->tap = [setter, idx]() {
            pop();                 // prima chiude la pagina di scelta...
            if (*setter) (*setter)(idx);  // ...poi applica (puo' aprire altre pagine)
            refresh();
        };
        lv_obj_add_event_cb(row, row_click_cb, LV_EVENT_CLICKED, r.get());
        p->rows.push_back(std::move(r));
    }
}

namespace {
void close_dialog() { if (g_dialog) { lv_obj_delete_async(g_dialog); g_dialog = nullptr; } }

lv_obj_t* dialog_card(const char* title, const std::string& text, int32_t& out_h) {
    close_dialog();
    // strato trasparente che blocca i tocchi sotto (nessun costo di disegno)
    g_dialog = w::box(g_host, 0, 0, g_w, g_h, kBg);
    lv_obj_set_style_bg_opa(g_dialog, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(g_dialog, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(g_dialog, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_remove_flag(g_dialog, LV_OBJ_FLAG_EVENT_BUBBLE);
    const int32_t dw = cw();
    lv_point_t sz;
    lv_text_get_size(&sz, text.c_str(), fontUnit(), 0, 4, dw - 2 * kCardPad, LV_TEXT_FLAG_NONE);
    out_h = kCardPad + 34 + sz.y + 24 + kRowH + kCardPad;
    lv_obj_t* cardo = w::card(g_dialog, kMargin, (g_h - out_h) / 2, dw, out_h);
    lv_obj_set_style_border_width(cardo, 1, 0);
    lv_obj_set_style_border_color(cardo, c(kTrack), 0);
    lv_obj_t* t = w::label(cardo, title, fontBody(), kText);
    lv_obj_set_pos(t, kCardPad, kCardPad);
    lv_obj_t* b = w::label(cardo, text.c_str(), fontUnit(), kSecondary);
    lv_obj_set_style_text_line_space(b, 4, 0);
    lv_label_set_long_mode(b, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(b, dw - 2 * kCardPad);
    lv_obj_set_pos(b, kCardPad, kCardPad + 34);
    return cardo;
}

lv_obj_t* dialog_button(lv_obj_t* parent, int32_t x, int32_t y, int32_t wdt, const char* txt, uint32_t bg, uint32_t fg) {
    lv_obj_t* b = w::box(parent, x, y, wdt, kRowH, bg, 18);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(b, c(kCardPress), LV_STATE_PRESSED);
    lv_obj_t* l = w::label(b, txt, fontBody(), fg);
    lv_obj_center(l);
    return b;
}
}  // namespace

void confirm(const char* title, const std::string& text, const char* ok_label, Fn on_ok, bool destructive) {
    int32_t h = 0;
    lv_obj_t* cardo = dialog_card(title, text, h);
    const int32_t bw = (cw() - 2 * kCardPad - kGap) / 2;
    const int32_t by = h - kCardPad - kRowH;
    lv_obj_t* no = dialog_button(cardo, kCardPad, by, bw, "Annulla", kTrack, kText);
    lv_obj_add_event_cb(no, [](lv_event_t*) { close_dialog(); }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* yes = dialog_button(cardo, kCardPad + bw + kGap, by, bw, ok_label,
                                  destructive ? kCrit : accent(), destructive ? kText : kBg);
    auto* f = new Fn(std::move(on_ok));
    lv_obj_add_event_cb(yes, [](lv_event_t* e) {
        Fn fn = *static_cast<Fn*>(lv_event_get_user_data(e));
        close_dialog();
        if (fn) fn();
    }, LV_EVENT_CLICKED, f);
    lv_obj_add_event_cb(yes, [](lv_event_t* e) { delete static_cast<Fn*>(lv_event_get_user_data(e)); }, LV_EVENT_DELETE, f);
}

void message(const char* title, const std::string& text) {
    int32_t h = 0;
    lv_obj_t* cardo = dialog_card(title, text, h);
    lv_obj_t* ok = dialog_button(cardo, kCardPad, h - kCardPad - kRowH, cw() - 2 * kCardPad, "OK", accent(), kBg);
    lv_obj_add_event_cb(ok, [](lv_event_t*) { close_dialog(); }, LV_EVENT_CLICKED, nullptr);
}

void textInput(const char* title, const std::string& initial, bool password,
               std::function<void(const std::string&)> done) {
    lv_obj_t* list = push(title);
    Page* p = page_of(list);
    lv_obj_remove_flag(list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t* ta = lv_textarea_create(list);
    lv_obj_remove_style_all(ta);
    lv_obj_set_pos(ta, kMargin, 8);
    lv_obj_set_size(ta, cw(), kRowH);
    lv_obj_set_style_bg_color(ta, c(kCard), 0);
    lv_obj_set_style_bg_opa(ta, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(ta, 18, 0);
    lv_obj_set_style_pad_hor(ta, kCardPad, 0);
    lv_obj_set_style_pad_ver(ta, 20, 0);
    lv_obj_set_style_text_font(ta, fontBody(), 0);
    lv_obj_set_style_text_color(ta, c(kText), 0);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_password_mode(ta, password);
    lv_textarea_set_text(ta, initial.c_str());
    lv_obj_t* kb = lv_keyboard_create(list);
    lv_obj_set_pos(kb, 0, kRowH + 28);
    lv_obj_set_size(kb, g_w, 420);
    lv_obj_set_style_bg_color(kb, c(kBg), 0);
    lv_obj_set_style_border_width(kb, 0, 0);
    lv_obj_set_style_bg_color(kb, c(kCard), LV_PART_ITEMS);
    lv_obj_set_style_bg_color(kb, c(kTrack), static_cast<lv_style_selector_t>(LV_PART_ITEMS | LV_STATE_CHECKED));
    lv_obj_set_style_text_color(kb, c(kText), LV_PART_ITEMS);
    lv_obj_set_style_text_font(kb, fontBody(), LV_PART_ITEMS);
    lv_obj_set_style_radius(kb, 12, LV_PART_ITEMS);
    lv_obj_set_style_shadow_width(kb, 0, LV_PART_ITEMS);
    lv_obj_set_style_border_width(kb, 0, LV_PART_ITEMS);
    lv_keyboard_set_textarea(kb, ta);
    auto* f = new std::function<void(const std::string&)>(std::move(done));
    lv_obj_add_event_cb(kb, [](lv_event_t* e) {
        lv_obj_t* k = static_cast<lv_obj_t*>(lv_event_get_target(e));
        auto* fn = static_cast<std::function<void(const std::string&)>*>(lv_event_get_user_data(e));
        const std::string txt = lv_textarea_get_text(lv_keyboard_get_textarea(k));
        const bool ok = lv_event_get_code(e) == LV_EVENT_READY;
        auto cb = *fn;
        pop();
        if (ok && cb) cb(txt);
    }, LV_EVENT_READY, f);
    lv_obj_add_event_cb(kb, [](lv_event_t*) { pop(); }, LV_EVENT_CANCEL, nullptr);
    lv_obj_add_event_cb(kb, [](lv_event_t* e) {
        delete static_cast<std::function<void(const std::string&)>*>(lv_event_get_user_data(e));
    }, LV_EVENT_DELETE, f);
    (void)p;
}

lv_obj_t* topList() { return g_stack.empty() ? nullptr : g_stack.back()->list; }

void refresh() {
    if (g_stack.empty()) return;
    Page* p = g_stack.back();
    for (auto& r : p->rows) {
        if (r->text && r->value) {
            const std::string s = r->text();
            if (s != r->last) {
                r->last = s;
                lv_label_set_text(r->value, s.c_str());
            }
        }
        if (r->sw && r->get_b) {
            const bool on = r->get_b();
            if (on != lv_obj_has_state(r->sw, LV_STATE_CHECKED)) {
                if (on) lv_obj_add_state(r->sw, LV_STATE_CHECKED);
                else lv_obj_remove_state(r->sw, LV_STATE_CHECKED);
            }
        }
    }
}

}  // namespace abarth::ui_v2::sp
