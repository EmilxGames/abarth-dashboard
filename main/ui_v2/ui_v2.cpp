#include "ui_v2/ui_v2.h"

#include <cstdio>
#include <cstring>
#include <ctime>
#include <esp_log.h>
#include <esp_timer.h>
#include "lvgl.h"

#include "can/can_manager.h"
#include "config/settings.h"
#include "display/lvgl_v8_port.h"
#include "obd/obd_manager.h"
#include "ui/ui.h"
#include "ui_v2/page_personale.h"
#include "ui_v2/pages.h"
#include "ui_v2/settings_pages.h"
#include "ui_v2/data_fmt.h"
#include "ui_v2/theme.h"
#include "ui_v2/widgets.h"

namespace abarth::ui_v2 {
namespace debug_shot { void start(); }
void markBuilt();
namespace bench { void start(); }

namespace {
constexpr const char* TAG = "ui_v2";
using namespace theme;
constexpr int kPages = static_cast<int>(Page::Count);
constexpr int32_t kPageTop = 56;      ///< sotto la barra di stato
constexpr int32_t kPageBottom = 985;  ///< sopra l'indicatore pagine

lv_obj_t* g_scr = nullptr;
lv_obj_t* g_time = nullptr;
lv_obj_t* g_conn = nullptr;
w::StatusDot g_conn_dot;
lv_obj_t* g_pages[kPages] = {};
lv_obj_t* g_dots[kPages] = {};
Page g_current = Page::Guida;
int32_t g_dots_x0 = 0;

constexpr int32_t kDot = 9, kDotActiveW = 30, kDotGap = 10;
constexpr int32_t kGearW = 72;   ///< pulsante rapido Impostazioni in basso a destra
constexpr int32_t kGearH = 48;   ///< + 8 px di area extra per lato = tocco 88x64
Page g_before_setup = Page::Guida;

void layout_dots() {
    // pallini da 9 px, quello attivo e' una pillola 30x9 nel colore accento
    int32_t x = g_dots_x0;
    for (int i = 0; i < kPages; ++i) {
        const bool act = i == static_cast<int>(g_current);
        const int32_t wdt = act ? kDotActiveW : kDot;
        lv_obj_set_pos(g_dots[i], x, kDotsY - kDot / 2);
        lv_obj_set_width(g_dots[i], wdt);
        lv_obj_set_style_bg_color(g_dots[i], c(act ? accent() : kDotIdle), 0);
        x += wdt + kDotGap;
    }
}

void update_status() {
    const auto mode = settings::current().connection_mode;
    const char* txt = "BT \xC2\xB7 OBD";
    uint32_t col = kWarn;
    if (mode == settings::ConnectionMode::DirectCan) {
        txt = "CAN";
        const auto st = can::can().state(0);
        col = st == can::CanState::Running ? kOk : (st == can::CanState::BusOff || st == can::CanState::Error) ? kCrit : kWarn;
    } else {
        if (mode == settings::ConnectionMode::WifiObd) txt = "WIFI \xC2\xB7 OBD";
        const auto st = obd::obd().state();
        col = st == obd::ObdState::Polling ? kOk : (st == obd::ObdState::Error ? kCrit : kWarn);
    }
    w::setText(g_conn, txt);
    // pallino a sinistra del testo di collegamento
    const int32_t tw = w::textWidth(txt, fontStatus());
    const int32_t right = LV_HOR_RES - kStatusX;
    lv_obj_set_x(g_conn, right - tw);
    lv_obj_set_x(g_conn_dot.obj(), right - tw - 10 - kStatusDot);
    g_conn_dot.set(true, col);

    time_t now = time(nullptr);
    struct tm ti;
    localtime_r(&now, &ti);
    char tbuf[8] = "";
    if (ti.tm_year + 1900 >= 2024) std::snprintf(tbuf, sizeof(tbuf), "%02d:%02d", ti.tm_hour, ti.tm_min);
    w::setText(g_time, tbuf);
}

void on_timer(lv_timer_t*) {
    static uint32_t n = 0;
    const int64_t now_ms = esp_timer_get_time() / 1000;
    if ((n & 1) == 0) ui::background_tick();   // 10 Hz come nella UI Classica
    if ((n % 10) == 0) update_status();
    ++n;
    pages::update(g_current, now_ms);
}

void gesture_cb(lv_event_t*) {
    if (personale::editing() || settings_pages::inSubPage()) return;  // editor/sottopagine: niente swipe
    const lv_dir_t d = lv_indev_get_gesture_dir(lv_indev_active());
    const int cur = static_cast<int>(g_current);
    if (d == LV_DIR_LEFT && cur < kPages - 1) showPage(static_cast<Page>(cur + 1));
    else if (d == LV_DIR_RIGHT && cur > 0) showPage(static_cast<Page>(cur - 1));
}

void gear_cb(lv_event_t*) {
    // Pulsante rapido: apre l'elenco Impostazioni; se sei gia' nelle
    // Impostazioni ti riporta alla pagina da cui eri venuto.
    if (personale::editing()) return;
    settings_pages::closeSubPages();
    if (g_current == Page::Setup) {
        showPage(g_before_setup);
    } else {
        g_before_setup = g_current;
        showPage(Page::Setup);
    }
}

void dots_tap_cb(lv_event_t* e) {
    // tocco nella fascia in basso: meta' sinistra = indietro, destra = avanti
    lv_point_t p;
    lv_indev_get_point(lv_indev_active(), &p);
    const int cur = static_cast<int>(g_current);
    if (p.x < LV_HOR_RES / 2 && cur > 0) showPage(static_cast<Page>(cur - 1));
    else if (p.x >= LV_HOR_RES / 2 && cur < kPages - 1) showPage(static_cast<Page>(cur + 1));
}
}  // namespace

Page debugCurrentPage() { return g_current; }

void showPage(Page p) {
    const int idx = static_cast<int>(p);
    if (idx < 0 || idx >= kPages) return;
    if (g_pages[static_cast<int>(g_current)] && p != g_current) {
        lv_obj_add_flag(g_pages[static_cast<int>(g_current)], LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_remove_flag(g_pages[idx], LV_OBJ_FLAG_HIDDEN);
    g_current = p;
    layout_dots();
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
    lv_obj_add_event_cb(g_scr, gesture_cb, LV_EVENT_GESTURE, nullptr);

    // Barra di stato: ora a sinistra, collegamento a destra
    g_time = w::label(g_scr, "", fontStatus(), kSecondary);
    lv_obj_set_pos(g_time, kStatusX, kStatusTop);
    g_conn = w::label(g_scr, "", fontStatus(), kDim);
    lv_obj_set_pos(g_conn, W - kStatusX - 80, kStatusTop);
    // pallino stato collegamento a sinistra del testo (posizione fissa)
    g_conn_dot.create(g_scr, W - kStatusX - 150, kStatusTop + 5);


    for (int i = 0; i < kPages; ++i) {
        g_pages[i] = w::box(g_scr, 0, kPageTop, W, kPageBottom - kPageTop, kBg);
        // trasparente: lo sfondo e' quello dello schermo, una passata in meno
        lv_obj_set_style_bg_opa(g_pages[i], LV_OPA_TRANSP, 0);
        lv_obj_add_flag(g_pages[i], LV_OBJ_FLAG_HIDDEN);
        pages::build(static_cast<Page>(i), g_pages[i], W, kPageBottom - kPageTop, kPageTop);
    }

    // Indicatore pagine + fascia toccabile
    const int32_t dots_w = (kPages - 1) * (kDot + kDotGap) + kDotActiveW;
    g_dots_x0 = (W - dots_w) / 2;
    // fascia dei pallini: toccabile solo fino all'ingranaggio, cosi' un tocco
    // sull'ingranaggio apre soltanto le Impostazioni
    lv_obj_t* tap = w::box(g_scr, 0, kPageBottom, W - kGearW - 24, H - kPageBottom, kBg);
    lv_obj_add_flag(tap, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(tap, dots_tap_cb, LV_EVENT_CLICKED, nullptr);
    for (int i = 0; i < kPages; ++i) g_dots[i] = w::box(g_scr, 0, 0, kDot, kDot, kDotIdle, kDot / 2);

    // Ingranaggio in basso a destra, sulla riga dei pallini (sotto l'ultima scheda)
    lv_obj_t* gear = w::box(g_scr, W - kGearW - 12, kDotsY - kGearH / 2, kGearW, kGearH, kBg);
    lv_obj_add_flag(gear, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(gear, 8);
    lv_obj_remove_flag(gear, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_set_style_bg_color(gear, c(kCardPress), LV_STATE_PRESSED);
    lv_obj_set_style_radius(gear, 16, 0);
    lv_obj_t* gl = w::label(gear, LV_SYMBOL_SETTINGS, &lv_font_montserrat_30, kSecondary);
    lv_obj_center(gl);
    lv_obj_add_event_cb(gear, gear_cb, LV_EVENT_CLICKED, nullptr);

    showPage(Page::Guida);
    update_status();
    lv_timer_create(on_timer, 50, nullptr);  // 20 Hz: si ridisegna solo cio' che cambia
    lv_scr_load(g_scr);
    markBuilt();   // da qui gli allarmi usano il banner Moderno
    debug_shot::start();
    bench::start();
    ESP_LOGI(TAG, "UI v2 Moderno costruita (%ldx%ld)", (long)W, (long)H);
    lvgl_port_unlock();
}

}  // namespace abarth::ui_v2
