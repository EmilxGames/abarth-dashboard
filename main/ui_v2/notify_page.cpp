#include "ui_v2/notify_page.h"

#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include "lvgl.h"

#include "config/notify_log.h"
#include "ui_v2/theme.h"
#include "ui_v2/widgets.h"

namespace abarth::ui_v2::notify_page {
using namespace theme;

namespace {
constexpr int kPageItems = 60;   ///< voci mostrate per volta (lista leggera)
lv_obj_t* g_scr = nullptr;
lv_obj_t* g_bell = nullptr;
lv_obj_t* g_badge = nullptr;
lv_obj_t* g_badge_lbl = nullptr;
lv_obj_t* g_panel = nullptr;
lv_obj_t* g_list = nullptr;
lv_obj_t* g_clear_lbl = nullptr;
lv_timer_t* g_clear_timer = nullptr;
int32_t g_top = 0, g_bottom = 0;
uint32_t g_rev_seen = 0;
int g_shown = 0;
bool g_clear_armed = false;

const char* const kMonths[] = {"gennaio", "febbraio", "marzo", "aprile", "maggio", "giugno", "luglio",
                               "agosto", "settembre", "ottobre", "novembre", "dicembre"};

uint32_t level_color(uint8_t lv) { return lv >= 2 ? kCrit : lv == 1 ? kWarn : kOk; }

std::string day_label(uint32_t epoch) {
    if (!epoch) return "Data sconosciuta";
    const time_t now = time(nullptr), t = epoch;
    struct tm a, b;
    localtime_r(&now, &a);
    localtime_r(&t, &b);
    if (a.tm_year == b.tm_year && a.tm_yday == b.tm_yday) return "Oggi";
    const time_t y = now - 86400;
    struct tm c;
    localtime_r(&y, &c);
    if (c.tm_year == b.tm_year && c.tm_yday == b.tm_yday) return "Ieri";
    char s[32];
    std::snprintf(s, sizeof(s), "%d %s %d", b.tm_mday, kMonths[b.tm_mon % 12], b.tm_year + 1900);
    return s;
}

void fill_list(bool more);

lv_obj_t* small_button(lv_obj_t* parent, int32_t x, int32_t y, int32_t w, const char* txt, uint32_t col, lv_event_cb_t cb) {
    lv_obj_t* b = w::box(parent, x, y, w, 52, kTrack, 16);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_set_style_bg_color(b, c(kCardPress), LV_STATE_PRESSED);
    lv_obj_t* l = w::label(b, txt, fontLabel(), col);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
    return b;
}

void add_row(const notify::Entry& e, int32_t& y, int32_t cw) {
    // riga: pallino di gravita', ora, titolo e dettaglio (altezze calcolate, niente layout)
    lv_point_t sz;
    lv_text_get_size(&sz, e.msg, fontLabel(), 0, 2, cw - 2 * 22 - 22, LV_TEXT_FLAG_NONE);
    const int32_t h = 18 + lv_font_get_line_height(fontUnit()) + 6 + (e.msg[0] ? sz.y : 0) + 18;
    lv_obj_t* r = w::card(g_list, kMargin, y, cw, h);
    lv_obj_set_style_radius(r, 18, 0);
    w::box(r, 20, 18 + 7, 12, 12, level_color(e.level), LV_RADIUS_CIRCLE);
    char tm_s[8] = "--:--";
    if (e.epoch) {
        const time_t t = e.epoch;
        struct tm lt;
        localtime_r(&t, &lt);
        std::snprintf(tm_s, sizeof(tm_s), "%02d:%02d", lt.tm_hour, lt.tm_min);
    }
    lv_obj_t* tl = w::label(r, e.title, fontUnit(), e.read ? kSecondary : kText);
    lv_label_set_long_mode(tl, LV_LABEL_LONG_DOT);
    lv_obj_set_width(tl, cw - 44 - 90);
    lv_obj_set_pos(tl, 44, 16);
    lv_obj_t* hl = w::label(r, tm_s, fontLabel(), kDim);
    lv_obj_align(hl, LV_ALIGN_TOP_RIGHT, -20, 20);
    if (e.msg[0]) {
        lv_obj_t* ml = w::label(r, e.msg, fontLabel(), kSecondary);
        lv_label_set_long_mode(ml, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_line_space(ml, 2, 0);
        lv_obj_set_width(ml, cw - 44 - 22);
        lv_obj_set_pos(ml, 44, 18 + lv_font_get_line_height(fontUnit()) + 6);
    }
    y += h + 10;
}

void fill_list(bool more) {
    const auto all = notify::list();
    const int32_t cw = lv_obj_get_width(g_scr) - 2 * kMargin;
    if (!more) { lv_obj_clean(g_list); g_shown = 0; }
    // si ricostruisce solo la parte nuova; "Mostra altre" viene sostituito
    int32_t y = 8;
    if (more && lv_obj_get_child_count(g_list) > 0) {
        lv_obj_t* last = lv_obj_get_child(g_list, lv_obj_get_child_count(g_list) - 1);   // pulsante "Mostra altre"
        y = lv_obj_get_y(last);
        lv_obj_delete(last);
    }
    if (all.empty()) {
        lv_obj_t* l = w::label(g_list, notify::showingSimulated() ? "Nessun avviso simulato." : "Nessun avviso.", fontUnit(), kDim);
        lv_obj_set_pos(l, kMargin + 8, 24);
        return;
    }
    std::string last_day = g_shown > 0 ? day_label(all[g_shown - 1].epoch) : "";
    const int end = std::min<int>(static_cast<int>(all.size()), g_shown + kPageItems);
    for (int i = g_shown; i < end; ++i) {
        const std::string d = day_label(all[i].epoch);
        if (d != last_day) {
            lv_obj_t* h = w::label(g_list, d.c_str(), fontLabel(), kDim, true);
            lv_obj_set_pos(h, kMargin + 8, y + 8);
            y += 40;
            last_day = d;
        }
        add_row(all[i], y, cw);
    }
    g_shown = end;
    if (g_shown < static_cast<int>(all.size())) {
        lv_obj_t* b = small_button(g_list, kMargin, y, cw, "Mostra altre", kText, [](lv_event_t*) { fill_list(true); });
        (void)b;
    } else {
        w::box(g_list, 0, y + 20, 1, 1, kBg);   // spazio in fondo
    }
}

void disarm_clear() {
    g_clear_armed = false;
    if (g_clear_lbl) lv_label_set_text(g_clear_lbl, "Cancella");
    if (g_clear_timer) { lv_timer_delete(g_clear_timer); g_clear_timer = nullptr; }
}

void build_panel() {
    const int32_t W = lv_obj_get_width(g_scr);
    g_panel = w::box(g_scr, 0, g_top, W, g_bottom - g_top, kBg);
    lv_obj_add_flag(g_panel, LV_OBJ_FLAG_CLICKABLE);   // non far passare i tocchi alle pagine sotto
    lv_obj_remove_flag(g_panel, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_remove_flag(g_panel, LV_OBJ_FLAG_EVENT_BUBBLE);
    // intestazione: indietro, titolo (e "Simulazione"), pulsanti
    lv_obj_t* back = w::box(g_panel, kMargin - 8, 0, 64, 64, kBg, 16);
    lv_obj_add_flag(back, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(back, c(kCardPress), LV_STATE_PRESSED);
    lv_obj_t* bl = w::label(back, LV_SYMBOL_LEFT, fontBody(), kText);
    lv_obj_center(bl);
    lv_obj_add_event_cb(back, [](lv_event_t*) { close(); }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* t = w::label(g_panel, "Notifiche", fontBody(), kText);
    lv_obj_set_pos(t, kMargin + 56, (64 - lv_font_get_line_height(fontBody())) / 2);
    if (notify::showingSimulated()) {
        lv_obj_t* sim = w::box(g_panel, kMargin + 56 + w::textWidth("Notifiche", fontBody()) + 12, 18, 120, 30, kWarn, 15);
        lv_obj_t* sl = w::label(sim, "Simulazione", fontLabel(), kBg);
        lv_obj_center(sl);
    }
    const int32_t bw = (W - 2 * kMargin - kGap) / 2;
    small_button(g_panel, kMargin, 70, bw, "Segna tutte lette", kText, [](lv_event_t*) {
        notify::markAllRead();
        fill_list(false);
        updateBadge();
    });
    lv_obj_t* clr = small_button(g_panel, kMargin + bw + kGap, 70, bw, "Cancella", kCrit, [](lv_event_t*) {
        // conferma: primo tocco "Conferma?", secondo tocco entro 4 s cancella
        if (!g_clear_armed) {
            g_clear_armed = true;
            lv_label_set_text(g_clear_lbl, "Tocca ancora per cancellare");
            g_clear_timer = lv_timer_create([](lv_timer_t*) { g_clear_timer = nullptr; disarm_clear(); }, 4000, nullptr);
            lv_timer_set_repeat_count(g_clear_timer, 1);
            return;
        }
        if (g_clear_timer) { lv_timer_delete(g_clear_timer); g_clear_timer = nullptr; }
        disarm_clear();
        notify::clear();
        fill_list(false);
        updateBadge();
    });
    g_clear_lbl = lv_obj_get_child(clr, 0);
    // elenco scorrevole
    g_list = w::box(g_panel, 0, 134, W, g_bottom - g_top - 134, kBg);
    lv_obj_add_flag(g_list, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(g_list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(g_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(g_list, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(g_list, LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_remove_flag(g_list, LV_OBJ_FLAG_GESTURE_BUBBLE);
    fill_list(false);
}
}  // namespace

void buildBell(lv_obj_t* scr, int32_t x, int32_t y, int32_t wd, int32_t ht, int32_t page_top, int32_t page_bottom) {
    g_scr = scr;
    g_top = page_top;
    g_bottom = page_bottom;
    g_bell = w::box(scr, x, y, wd, ht, kBg, 16);
    lv_obj_set_style_bg_opa(g_bell, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(g_bell, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(g_bell, c(kCardPress), LV_STATE_PRESSED);
    lv_obj_add_flag(g_bell, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(g_bell, 8);
    lv_obj_remove_flag(g_bell, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_t* ic = w::label(g_bell, LV_SYMBOL_BELL, &lv_font_montserrat_28, kSecondary);
    lv_obj_center(ic);
    lv_obj_add_event_cb(g_bell, [](lv_event_t*) { if (isOpen()) close(); else open(); }, LV_EVENT_CLICKED, nullptr);
    // numero delle non lette
    lv_obj_add_flag(g_bell, LV_OBJ_FLAG_OVERFLOW_VISIBLE);   // il numero sporge in alto
    g_badge = w::box(g_bell, wd / 2 + 2, -8, 28, 26, kCrit, 13);
    g_badge_lbl = w::label(g_badge, "", fontLabel(), kText);
    lv_obj_center(g_badge_lbl);
    lv_obj_add_flag(g_badge, LV_OBJ_FLAG_HIDDEN);
    updateBadge();
}

void updateBadge() {
    if (!g_badge) return;
    const uint32_t rev = notify::revision() ^ (notify::showingSimulated() ? 0x80000000u : 0);
    if (rev == g_rev_seen) return;
    g_rev_seen = rev;
    const int n = notify::unread();
    if (n <= 0) {
        lv_obj_add_flag(g_badge, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    char b[8];
    std::snprintf(b, sizeof(b), "%s", n > 99 ? "99+" : std::to_string(n).c_str());
    lv_label_set_text(g_badge_lbl, b);
    lv_obj_set_width(g_badge, n > 9 ? 42 : 28);
    lv_obj_remove_flag(g_badge, LV_OBJ_FLAG_HIDDEN);
    if (isOpen()) fill_list(false);   // arrivato un avviso mentre il pannello e' aperto
}

void open() {
    if (g_panel) return;
    build_panel();
    lv_obj_move_foreground(g_panel);
}

void close() {
    if (!g_panel) return;
    disarm_clear();
    lv_obj_delete_async(g_panel);
    g_panel = nullptr;
    g_list = nullptr;
    g_clear_lbl = nullptr;
}

bool isOpen() { return g_panel != nullptr; }

void setBellVisible(bool on) {
    if (!g_bell) return;
    if (on) lv_obj_remove_flag(g_bell, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(g_bell, LV_OBJ_FLAG_HIDDEN);
}

}  // namespace abarth::ui_v2::notify_page
