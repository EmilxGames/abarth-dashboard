// Pagina IMPOSTAZIONI (design "Schede"): una scheda per gruppo, righe da 72 px
// separate da linee di 1 px; etichetta a sinistra, valore a destra. Le azioni
// sono quelle di settings_model (stesse funzioni della UI Classica).
#include <cstdio>
#include <cstring>
#include <esp_system.h>
#include <esp_timer.h>
#include "lvgl.h"
#include "ui/settings_tab.h"
#include "ui_v2/page_setup.h"
#include "ui_v2/settings_model.h"
#include "ui_v2/theme.h"
#include "ui_v2/widgets.h"

namespace abarth::ui_v2::setup_page {

namespace {
using namespace theme;
using setup::RowId;

constexpr int kMaxRows = static_cast<int>(RowId::Count);
lv_obj_t* g_page = nullptr;
lv_obj_t* g_list = nullptr;
lv_obj_t* g_values[kMaxRows] = {};
lv_obj_t* g_bright_val = nullptr;
lv_obj_t* g_toast = nullptr;
lv_obj_t* g_toast_lbl = nullptr;
lv_obj_t* g_adv_host = nullptr;
int64_t   g_toast_until = 0;
bool      g_reboot_pending = false;
int32_t   g_w = 0, g_h = 0;

void refresh_values() {
    char buf[40];
    for (int i = 0; i < kMaxRows; ++i) {
        if (!g_values[i]) continue;
        setup::valueText(static_cast<RowId>(i), buf, sizeof(buf));
        w::setText(g_values[i], buf);
    }
}

void toast(const char* msg, bool reboot) {
    g_reboot_pending = g_reboot_pending || reboot;
    char buf[160];
    std::snprintf(buf, sizeof(buf), "%s%s", msg, g_reboot_pending ? "\nTocca qui per riavviare ora." : "");
    lv_label_set_text(g_toast_lbl, buf);
    lv_obj_remove_flag(g_toast, LV_OBJ_FLAG_HIDDEN);
    g_toast_until = esp_timer_get_time() / 1000 + 6000;
}

void toast_cb(lv_event_t*) {
    if (g_reboot_pending) esp_restart();
    lv_obj_add_flag(g_toast, LV_OBJ_FLAG_HIDDEN);
}

void adv_close_cb(lv_event_t*) { if (g_adv_host) lv_obj_add_flag(g_adv_host, LV_OBJ_FLAG_HIDDEN); }

void open_advanced() {
    if (!g_adv_host) {
        // Impostazioni complete della UI Classica, costruite al primo uso
        g_adv_host = w::box(g_page, 0, 0, g_w, g_h, kBg);
        lv_obj_remove_flag(g_adv_host, LV_OBJ_FLAG_GESTURE_BUBBLE);
        lv_obj_t* back = w::box(g_adv_host, 0, 0, g_w, 56, kCard);
        lv_obj_add_flag(back, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_t* bl = w::label(back, LV_SYMBOL_LEFT "  Impostazioni", fontBody(), kText);
        lv_obj_align(bl, LV_ALIGN_LEFT_MID, kCardPad, 0);
        lv_obj_add_event_cb(back, adv_close_cb, LV_EVENT_CLICKED, nullptr);
        lv_obj_t* host = w::box(g_adv_host, 0, 56, g_w, g_h - 56, kBg);
        lv_obj_remove_flag(host, LV_OBJ_FLAG_GESTURE_BUBBLE);
        ui::settings_tab::build(host);
    }
    lv_obj_remove_flag(g_adv_host, LV_OBJ_FLAG_HIDDEN);
}

void row_cb(lv_event_t* e) {
    const auto id = static_cast<RowId>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
    if (id == RowId::Advanced) { open_advanced(); return; }
    bool reboot = false;
    const char* msg = setup::activate(id, reboot);
    refresh_values();
    if (msg) toast(msg, reboot);
}

void slider_cb(lv_event_t* e) {
    lv_obj_t* s = static_cast<lv_obj_t*>(lv_event_get_target(e));
    const int v = lv_slider_get_value(s);
    if (lv_event_get_code(e) == LV_EVENT_RELEASED) setup::brightnessCommit(v);
    else setup::brightnessPreview(v);
    char buf[12];
    std::snprintf(buf, sizeof(buf), "%d %%", v);
    w::setText(g_bright_val, buf);
}
}  // namespace

void build(lv_obj_t* parent, int32_t W, int32_t H) {
    g_page = parent;
    g_w = W;
    g_h = H;
    // Lista scorrevole verticale, senza rimbalzi (meno ridisegni)
    g_list = w::box(parent, 0, 0, W, H, kBg);
    lv_obj_set_style_bg_opa(g_list, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(g_list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(g_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(g_list, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(g_list, LV_OBJ_FLAG_SCROLL_ELASTIC);

    int n = 0;
    const setup::RowInfo* rows = setup::rows(n);
    const int32_t cw = W - 2 * kMargin;
    int32_t y = 10;
    int i = 0;
    while (i < n) {
        // gruppo = righe consecutive con la stessa sezione
        const char* sec = rows[i].section;
        int j = i;
        int32_t group_h = 0;
        while (j < n && std::strcmp(rows[j].section, sec) == 0) {
            group_h += (rows[j].id == RowId::Brightness ? kRowH + 56 : kRowH) + (j > i ? 1 : 0);
            ++j;
        }
        lv_obj_t* st = w::label(g_list, sec, fontLabel(), kDim, true);
        lv_obj_set_pos(st, kMargin + kCardPad, y);
        y += 34;
        lv_obj_t* card = w::card(g_list, kMargin, y, cw, group_h);
        lv_obj_set_style_clip_corner(card, false, 0);
        int32_t ry = 0;
        for (int k = i; k < j; ++k) {
            const RowId id = rows[k].id;
            if (k > i) { w::box(card, kCardPad, ry, cw - 2 * kCardPad, 1, kRowLine); ry += 1; }
            const int32_t rh = id == RowId::Brightness ? kRowH + 56 : kRowH;
            lv_obj_t* row = w::box(card, 0, ry, cw, rh, kCard);
            // riga trasparente (la scheda sotto ha gia' il colore): si colora solo
            // quando e' premuta, cosi' non si ridisegna due volte la stessa area
            lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
            lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_STATE_PRESSED);
            if (k == i || k == j - 1) lv_obj_set_style_radius(row, kRadius, 0);
            lv_obj_t* l = w::label(row, rows[k].title, fontBody(), kText);
            lv_obj_set_pos(l, kCardPad, (kRowH - lv_font_get_line_height(fontBody())) / 2);
            if (id == RowId::Brightness) {
                g_bright_val = w::label(row, "", fontBody(), kSecondary);
                lv_obj_set_style_text_align(g_bright_val, LV_TEXT_ALIGN_RIGHT, 0);
                lv_obj_set_width(g_bright_val, 120);
                lv_obj_set_pos(g_bright_val, cw - kCardPad - 120, (kRowH - lv_font_get_line_height(fontBody())) / 2);
                lv_obj_t* sl = lv_slider_create(row);
                lv_obj_remove_style_all(sl);
                // il pomello sporge di 8+6 px: la traccia rientra per non uscire dalla scheda
                lv_obj_set_pos(sl, kCardPad + 14, kRowH + 8);
                lv_obj_set_size(sl, cw - 2 * kCardPad - 28, kPillH);
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
                lv_slider_set_range(sl, 10, 100);
                lv_slider_set_value(sl, setup::brightness(), LV_ANIM_OFF);
                lv_obj_remove_flag(sl, LV_OBJ_FLAG_GESTURE_BUBBLE);
                lv_obj_add_event_cb(sl, slider_cb, LV_EVENT_VALUE_CHANGED, nullptr);
                lv_obj_add_event_cb(sl, slider_cb, LV_EVENT_RELEASED, nullptr);
                char buf[12];
                std::snprintf(buf, sizeof(buf), "%d %%", setup::brightness());
                lv_label_set_text(g_bright_val, buf);
            } else {
                lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
                lv_obj_set_style_bg_color(row, c(kCardPress), LV_STATE_PRESSED);
                lv_obj_add_event_cb(row, row_cb, LV_EVENT_CLICKED, reinterpret_cast<void*>(static_cast<uintptr_t>(id)));
                lv_obj_t* v = w::label(row, id == RowId::Advanced ? LV_SYMBOL_RIGHT : "", fontBody(), kSecondary);
                lv_obj_set_style_text_align(v, LV_TEXT_ALIGN_RIGHT, 0);
                lv_obj_set_width(v, 260);
                lv_obj_set_pos(v, cw - kCardPad - 260, (kRowH - lv_font_get_line_height(fontBody())) / 2);
                if (id != RowId::Advanced) g_values[static_cast<int>(id)] = v;
            }
            ry += rh;
        }
        y += group_h + kGap + 6;
        i = j;
    }
    // spazio finale per poter scorrere fino in fondo
    w::box(g_list, 0, y, 1, 20, kBg);

    // Messaggio non modale in basso (riavvio richiesto, avvisi)
    g_toast = w::card(parent, kMargin, H - 150, cw, 130);
    lv_obj_set_style_border_width(g_toast, 1, 0);
    lv_obj_set_style_border_color(g_toast, c(kWarn), 0);
    lv_obj_add_flag(g_toast, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(g_toast, toast_cb, LV_EVENT_CLICKED, nullptr);
    g_toast_lbl = w::label(g_toast, "", fontBody(), kText);
    lv_obj_set_width(g_toast_lbl, cw - 2 * kCardPad);
    lv_label_set_long_mode(g_toast_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(g_toast_lbl, kCardPad, 20);
    lv_obj_add_flag(g_toast, LV_OBJ_FLAG_HIDDEN);

    refresh_values();
}

lv_obj_t* scrollTarget() { return g_list; }

void update(int64_t now_ms) {
    static int64_t last = 0;
    if (now_ms - last >= 1000) {  // valori come "Web Server: attivo" possono cambiare da soli
        last = now_ms;
        refresh_values();
    }
    if (g_toast_until && now_ms > g_toast_until && !g_reboot_pending) {
        g_toast_until = 0;
        lv_obj_add_flag(g_toast, LV_OBJ_FLAG_HIDDEN);
    }
    if (g_adv_host && !lv_obj_has_flag(g_adv_host, LV_OBJ_FLAG_HIDDEN)) {
        static uint32_t n = 0;
        if ((n++ % 2) == 0) ui::settings_tab::tick();
    }
}

}  // namespace abarth::ui_v2::setup_page
