#include "ui_v2/ui_v2.h"

#include <cstdio>
#include <cstring>
#include <ctime>
#include <esp_log.h>
#include <esp_timer.h>
#include "lvgl.h"

#include "can/can_manager.h"
#include "config/settings.h"
#include "config/warmup_manager.h"
#include "display/lvgl_v8_port.h"
#include "obd/obd_manager.h"
#include "ui/ui.h"
#include "ui_v2/page_personale.h"
#include "ui_v2/pages.h"
#include "ui_v2/settings_pages.h"
#include "ui_v2/data_fmt.h"
#include "ui_v2/alert_levels.h"
#include "ui_v2/theme.h"
#include "ui_v2/widgets.h"

extern "C" {
extern const lv_image_dsc_t icon_thermo;   // ui_v2/icons.c
extern const lv_image_dsc_t icon_oil;
}

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

constexpr int32_t kDot = 12, kDotActiveW = 38, kDotGap = 16;   // pallini piu' visibili
lv_obj_t* g_title = nullptr;          ///< nome della pagina al centro della barra di stato
// Indicatore riscaldamento: due icone (termometro acqua e oliatore) con un
// anello sottile di avanzamento intorno; accanto all'oliatore il tempo mm:ss.
struct WarmIcon {
    lv_obj_t* arc = nullptr;
    lv_obj_t* img = nullptr;
    uint32_t col = 0xFFFFFFFF;      ///< colore attuale dell'icona
    uint32_t arc_col = 0xFFFFFFFF;  ///< colore attuale dell'anello
    int pct = -1;                   ///< avanzamento attuale dell'anello
};
WarmIcon g_wi_water, g_wi_oil;
lv_obj_t* g_warm_time = nullptr;    ///< conto alla rovescia accanto all'oliatore
bool  g_warm_blink = false;         ///< fase del lampeggio (1 Hz)
float g_water_start = 20.0f;        ///< inizio scala dell'anello acqua
constexpr uint32_t kIce = 0x7FD4FF; ///< azzurro ghiaccio: freddo
lv_timer_t* g_title_timer = nullptr;
const char* const kPageNames[] = {"GUIDA", "TEMPERATURE", "PRESSIONI", "ELETTRICO", "PERSONALE", "IMPOSTAZIONI"};
constexpr int32_t kGearW = 72;   ///< pulsante rapido Impostazioni in basso a destra
constexpr int32_t kGearH = 48;   ///< + 8 px di area extra per lato = tocco 88x64
Page g_before_setup = Page::Guida;

int32_t g_title_dx = 0;
/// Spostamento a destra del nome pagina perche' non tocchi le icone del
/// riscaldamento e il conto alla rovescia (di solito 0: resta centrato)
int32_t title_dx() {
    if (!g_title || !g_wi_oil.arc) return 0;
    lv_point_t p;
    lv_text_get_size(&p, lv_label_get_text(g_title), lv_obj_get_style_text_font(g_title, LV_PART_MAIN),
                     lv_obj_get_style_text_letter_space(g_title, LV_PART_MAIN), 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    int32_t right = lv_obj_get_x(g_wi_water.arc) + 36;
    if (!lv_obj_has_flag(g_wi_oil.arc, LV_OBJ_FLAG_HIDDEN)) right = lv_obj_get_x(g_wi_oil.arc) + 36;
    const char* cd = lv_label_get_text(g_warm_time);
    if (cd && cd[0]) right = lv_obj_get_x(g_warm_time) + w::textWidth(cd, fontStatus());
    const int32_t left = (LV_HOR_RES - p.x) / 2;
    const int32_t need = right + 14 - left;
    return need > 0 ? need : 0;
}
void align_title(int32_t y) {
    g_title_dx = title_dx();
    lv_obj_align(g_title, LV_ALIGN_TOP_MID, g_title_dx, y);
}

void title_settle_cb(lv_timer_t* t) {
    // dopo ~1 s il nome torna alla dimensione normale (si ridisegna solo l'etichetta)
    lv_obj_set_style_text_font(g_title, fontStatus(), 0);
    lv_obj_set_style_text_letter_space(g_title, kLabelLetterSpace, 0);
    align_title(kStatusTop);
    lv_timer_delete(t);
    g_title_timer = nullptr;
}

void show_title(int idx) {
    lv_label_set_text(g_title, kPageNames[idx]);
    lv_obj_set_style_text_font(g_title, fontBody(), 0);
    lv_obj_set_style_text_letter_space(g_title, 4, 0);
    align_title(kStatusTop - 3);
    if (g_title_timer) lv_timer_reset(g_title_timer);
    else g_title_timer = lv_timer_create(title_settle_cb, 1000, nullptr);
}

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

void warm_set(WarmIcon& wi, uint32_t col, int pct, uint32_t arc_col = 0xFFFFFFFF) {
    // ridisegna solo cio' che cambia: colore dell'icona, dell'anello, avanzamento
    if (arc_col == 0xFFFFFFFF) arc_col = col;
    if (col != wi.col) {
        wi.col = col;
        lv_obj_set_style_image_recolor(wi.img, c(col), 0);
    }
    if (arc_col != wi.arc_col) {
        wi.arc_col = arc_col;
        lv_obj_set_style_arc_color(wi.arc, c(arc_col), LV_PART_INDICATOR);
    }
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    if (pct != wi.pct) {
        wi.pct = pct;
        lv_arc_set_value(wi.arc, pct);
    }
}

void update_warm() {
    // logica condivisa con la Classica (warmup_mgr): qui solo la presentazione
    using warmup::WarmupState;
    const auto& s = settings::current();
    const float thr = s.oil_warmup_water_temp;

    // Acqua: anello azzurro che si riempie da ~20 °C (o dal valore all'avvio
    // se piu' basso) fino alla soglia; verde a soglia raggiunta, rosso solo
    // se surriscaldata (soglia dell'allarme temperatura acqua)
    const auto wv = data::store().get(data::Metric::CoolantTempC);
    if (!wv.valid()) {
        warm_set(g_wi_water, kDim, 0);
    } else {
        if (wv.value < g_water_start) g_water_start = wv.value;
        if (alertLevel(data::Metric::CoolantTempC, wv.value) == Level::Warn) {
            warm_set(g_wi_water, kCrit, 100);
        } else if (wv.value >= thr) {
            warm_set(g_wi_water, kOk, 100);
        } else {
            const float span = thr - g_water_start;
            const int pct = span > 0.5f ? static_cast<int>((wv.value - g_water_start) * 100.0f / span) : 0;
            warm_set(g_wi_water, kIce, pct);
        }
    }

    // Olio: azzurro e anello vuoto finche' l'acqua non e' a soglia; durante il
    // conto alla rovescia lampeggia ambra e l'anello si riempie; poi verde fisso
    const WarmupState st = warmup::warmup_mgr().state();
    const bool hidden = st == WarmupState::Disabled || !s.oil_warmup_enabled;
    if (hidden != lv_obj_has_flag(g_wi_oil.arc, LV_OBJ_FLAG_HIDDEN)) {
        if (hidden) lv_obj_add_flag(g_wi_oil.arc, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_remove_flag(g_wi_oil.arc, LV_OBJ_FLAG_HIDDEN);
    }
    char tb[8] = "";
    if (hidden) {
        g_warm_blink = false;
    } else if (st == WarmupState::Warming) {
        g_warm_blink = !g_warm_blink;   // update_warm gira ogni 0,5 s -> 1 Hz
        const int pct = static_cast<int>(warmup::warmup_mgr().progressRatio() * 100.0f);
        warm_set(g_wi_oil, g_warm_blink ? kWarn : kDim, pct, kWarn);   // l'anello resta ambra
        const uint16_t r = warmup::warmup_mgr().remainingSeconds();
        std::snprintf(tb, sizeof(tb), "%u:%02u", r / 60, r % 60);
    } else if (st == WarmupState::Ready) {
        warm_set(g_wi_oil, kOk, 100);
    } else {
        warm_set(g_wi_oil, kIce, 0);
    }
    w::setText(g_warm_time, tb);
    // il nome pagina si sposta solo se il conto alla rovescia lo toccherebbe
    if (!g_title_timer && title_dx() != g_title_dx) align_title(kStatusTop);
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

    update_warm();

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
    lv_indev_t* indev = lv_indev_active();
    const lv_dir_t d = lv_indev_get_gesture_dir(indev);
    // un solo cambio pagina per swipe: il resto del movimento viene ignorato
    // fino al rilascio (altrimenti le schede toccabili della pagina nuova,
    // es. PERSONALE, "riprendono" il gesto e si salta una pagina)
    if (d == LV_DIR_LEFT || d == LV_DIR_RIGHT) lv_indev_wait_release(indev);
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
bool debugWarmBlink() { return g_warm_blink; }

void showPage(Page p) {
    const int idx = static_cast<int>(p);
    if (idx < 0 || idx >= kPages) return;
    if (g_pages[static_cast<int>(g_current)] && p != g_current) {
        lv_obj_add_flag(g_pages[static_cast<int>(g_current)], LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_remove_flag(g_pages[idx], LV_OBJ_FLAG_HIDDEN);
    g_current = p;
    layout_dots();
    if (g_title) show_title(idx);
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
    // Indicatore riscaldamento dopo l'ora: due icone da 20 px dentro anelli
    // da 36 px (3 px), centrati sulla riga dei testi della barra di stato
    {
        const int32_t cy = kStatusTop + lv_font_get_line_height(fontStatus()) / 2;
        auto make = [&](WarmIcon& wi, int32_t x, const lv_image_dsc_t* src) {
            wi.arc = lv_arc_create(g_scr);
            lv_obj_set_size(wi.arc, 36, 36);
            lv_obj_set_pos(wi.arc, x, cy - 18);
            lv_arc_set_rotation(wi.arc, 270);
            lv_arc_set_bg_angles(wi.arc, 0, 360);
            lv_arc_set_range(wi.arc, 0, 100);
            lv_arc_set_value(wi.arc, 0);
            lv_obj_remove_style(wi.arc, nullptr, LV_PART_KNOB);
            lv_obj_remove_flag(wi.arc, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_style_arc_width(wi.arc, 3, LV_PART_MAIN);
            lv_obj_set_style_arc_color(wi.arc, c(kTrack), LV_PART_MAIN);
            lv_obj_set_style_arc_width(wi.arc, 3, LV_PART_INDICATOR);
            lv_obj_set_style_arc_rounded(wi.arc, true, LV_PART_INDICATOR);
            lv_obj_set_style_bg_opa(wi.arc, LV_OPA_TRANSP, 0);
            lv_obj_set_style_pad_all(wi.arc, 0, 0);
            wi.img = lv_image_create(wi.arc);
            lv_image_set_src(wi.img, src);
            lv_obj_set_style_image_recolor_opa(wi.img, LV_OPA_COVER, 0);
            lv_obj_center(wi.img);
            warm_set(wi, kDim, 0);
        };
        const int32_t x0 = kStatusX + 66;
        make(g_wi_water, x0, &icon_thermo);
        make(g_wi_oil, x0 + 44, &icon_oil);
        g_warm_time = w::label(g_scr, "", fontStatus(), kWarn);
        lv_obj_set_pos(g_warm_time, x0 + 44 + 36 + 8, kStatusTop);
    }

    g_title = w::label(g_scr, "", fontStatus(), kText, true);
    lv_obj_align(g_title, LV_ALIGN_TOP_MID, 0, kStatusTop);
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
