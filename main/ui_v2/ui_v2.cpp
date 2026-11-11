#include "ui_v2/ui_v2.h"

#include <cstdio>
#include <cstring>
#include <ctime>
#include <esp_log.h>
#include <esp_system.h>
#include <esp_timer.h>
#include "lvgl.h"

#include "can/can_manager.h"
#include "config/settings.h"
#include "display/lvgl_v8_port.h"
#include "obd/obd_manager.h"
#include "ui/settings_tab.h"
#include "ui/ui.h"
#include "ui_v2/pages.h"
#include "ui_v2/theme.h"

namespace abarth::ui_v2 {

namespace {
constexpr const char* TAG = "ui_v2";
using namespace theme;

lv_obj_t* g_scr = nullptr;
lv_obj_t* g_status_conn = nullptr;
lv_obj_t* g_status_time = nullptr;
lv_obj_t* g_pages[static_cast<int>(Page::Count)] = {};
lv_obj_t* g_nav_btn[static_cast<int>(Page::Count)] = {};
Page g_current = Page::Guida;

const char* const kNavNames[] = {"GUIDA", "TEMP", "PRESS", "ELETTR", "PERSON.", "SETUP"};

/// Oggetto rettangolare piatto e opaco, senza stile di default.
lv_obj_t* flat_box(lv_obj_t* parent, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t bg) {
    lv_obj_t* o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, c(bg), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

void set_label_if_changed(lv_obj_t* lbl, const char* txt) {
    if (std::strcmp(lv_label_get_text(lbl), txt) != 0) lv_label_set_text(lbl, txt);
}

void update_status(int64_t /*now_ms*/) {
    // Stato collegamento
    const auto mode = settings::current().connection_mode;
    const char* txt = "";
    uint32_t col = kMuted;
    if (mode == settings::ConnectionMode::DirectCan) {
        const auto st = can::can().state(0);
        txt = can::canStateName(st);
        col = (st == can::CanState::Running) ? kGreen
            : (st == can::CanState::BusOff || st == can::CanState::Error) ? kRed : kYellow;
    } else {
        const auto st = obd::obd().state();
        txt = obd::stateName(st);
        col = (st == obd::ObdState::Polling) ? kGreen
            : (st == obd::ObdState::Error) ? kRed : kYellow;
    }
    char buf[48];
    std::snprintf(buf, sizeof(buf), "%s %s",
                  mode == settings::ConnectionMode::DirectCan ? "CAN" : "OBD", txt);
    if (std::strcmp(lv_label_get_text(g_status_conn), buf) != 0) {
        lv_label_set_text(g_status_conn, buf);
        lv_obj_set_style_text_color(g_status_conn, c(col), 0);
    }

    // Orologio (solo se l'RTC ha un'ora valida)
    time_t now = time(nullptr);
    struct tm ti;
    localtime_r(&now, &ti);
    char tbuf[8] = "";
    if (ti.tm_year + 1900 >= 2024) std::snprintf(tbuf, sizeof(tbuf), "%02d:%02d", ti.tm_hour, ti.tm_min);
    set_label_if_changed(g_status_time, tbuf);
}

void on_timer(lv_timer_t*) {
    static uint32_t n = 0;
    const int64_t now_ms = esp_timer_get_time() / 1000;
    // Logica comune (demo, allarmi, warmup) a 10 Hz come nella UI Classica
    if ((n++ & 1) == 0) ui::background_tick();
    if ((n % 10) == 0) update_status(now_ms);
    pages::update(g_current, now_ms);
}

void build_status_bar(int32_t W) {
    lv_obj_t* bar = flat_box(g_scr, 0, 0, W, kStatusH, kBg);
    lv_obj_t* brand = lv_label_create(bar);
    lv_label_set_text(brand, "ABARTH");
    lv_obj_set_style_text_color(brand, c(kRed), 0);
    lv_obj_set_style_text_font(brand, fontLabel(), 0);
    lv_obj_align(brand, LV_ALIGN_LEFT_MID, kPad, 0);

    g_status_conn = lv_label_create(bar);
    lv_label_set_text(g_status_conn, "");
    lv_obj_set_style_text_font(g_status_conn, fontSmall(), 0);
    lv_obj_align(g_status_conn, LV_ALIGN_CENTER, 0, 0);

    g_status_time = lv_label_create(bar);
    lv_label_set_text(g_status_time, "");
    lv_obj_set_style_text_color(g_status_time, c(kText), 0);
    lv_obj_set_style_text_font(g_status_time, fontLabel(), 0);
    lv_obj_align(g_status_time, LV_ALIGN_RIGHT_MID, -kPad, 0);

    // linea sottile sotto la barra
    flat_box(g_scr, 0, kStatusH - 1, W, 1, kLine);
}

void nav_cb(lv_event_t* e) {
    const auto idx = static_cast<int>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
    showPage(static_cast<Page>(idx));
}

void build_nav_bar(int32_t W, int32_t H) {
    const int n = static_cast<int>(Page::Count);
    flat_box(g_scr, 0, H - kNavH, W, 1, kLine);
    const int32_t bw = W / n;
    for (int i = 0; i < n; ++i) {
        const int32_t x = i * bw;
        const int32_t w = (i == n - 1) ? (W - x) : bw;
        lv_obj_t* b = flat_box(g_scr, x, H - kNavH + 1, w, kNavH - 1, kBg);
        lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_color(b, c(kPanelHi), LV_STATE_PRESSED);
        // indicatore pagina attiva: bordo superiore rosso (piccola area)
        lv_obj_set_style_border_side(b, LV_BORDER_SIDE_TOP, LV_STATE_CHECKED);
        lv_obj_set_style_border_width(b, 4, LV_STATE_CHECKED);
        lv_obj_set_style_border_color(b, c(kRed), LV_STATE_CHECKED);
        lv_obj_t* l = lv_label_create(b);
        lv_label_set_text(l, kNavNames[i]);
        lv_obj_set_style_text_font(l, fontSmall(), 0);
        lv_obj_set_style_text_color(l, c(kMuted), 0);
        lv_obj_set_style_text_color(l, c(kText), LV_STATE_CHECKED);
        lv_obj_center(l);
        lv_obj_add_event_cb(b, nav_cb, LV_EVENT_CLICKED, reinterpret_cast<void*>(static_cast<uintptr_t>(i)));
        g_nav_btn[i] = b;
    }
}
}  // namespace

void showPage(Page p) {
    const int idx = static_cast<int>(p);
    if (idx < 0 || idx >= static_cast<int>(Page::Count)) return;
    for (int i = 0; i < static_cast<int>(Page::Count); ++i) {
        if (!g_pages[i]) continue;
        if (i == idx) {
            lv_obj_remove_flag(g_pages[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_state(g_nav_btn[i], LV_STATE_CHECKED);
            lv_obj_add_state(lv_obj_get_child(g_nav_btn[i], 0), LV_STATE_CHECKED);
        } else {
            lv_obj_add_flag(g_pages[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_state(g_nav_btn[i], LV_STATE_CHECKED);
            lv_obj_remove_state(lv_obj_get_child(g_nav_btn[i], 0), LV_STATE_CHECKED);
        }
    }
    g_current = p;
    pages::onShow(p);
}

void build() {
    if (!lvgl_port_lock(-1)) return;

    const int32_t W = LV_HOR_RES;
    const int32_t H = LV_VER_RES;

    g_scr = lv_obj_create(nullptr);
    lv_obj_remove_style_all(g_scr);
    lv_obj_set_size(g_scr, W, H);
    lv_obj_set_style_bg_color(g_scr, c(kBg), 0);
    lv_obj_set_style_bg_opa(g_scr, LV_OPA_COVER, 0);
    lv_obj_remove_flag(g_scr, LV_OBJ_FLAG_SCROLLABLE);

    build_status_bar(W);

    const int32_t content_y = kStatusH;
    const int32_t content_h = H - kStatusH - kNavH;
    for (int i = 0; i < static_cast<int>(Page::Count); ++i) {
        g_pages[i] = flat_box(g_scr, 0, content_y, W, content_h, kBg);
        lv_obj_add_flag(g_pages[i], LV_OBJ_FLAG_HIDDEN);
        pages::build(static_cast<Page>(i), g_pages[i], W, content_h);
    }

    build_nav_bar(W, H);
    showPage(Page::Guida);

    lv_timer_create(on_timer, 50, nullptr);  // 20 Hz: valori aggiornati solo se cambiano
    lv_scr_load(g_scr);
    ESP_LOGI(TAG, "UI v2 Racing costruita (%ldx%ld)", (long)W, (long)H);
    lvgl_port_unlock();
}

}  // namespace abarth::ui_v2
