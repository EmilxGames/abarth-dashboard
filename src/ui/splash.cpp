#include "ui/splash.h"

#include <lvgl.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_system.h>

#include <cstring>

#include "display/lvgl_v8_port.h"

namespace abarth::ui::splash {

namespace {
constexpr const char* TAG = "splash";

lv_obj_t*   g_screen       = nullptr;
lv_obj_t*   g_bar          = nullptr;
lv_obj_t*   g_status_label = nullptr;
lv_timer_t* g_progress_tmr = nullptr;

int32_t g_progress = 0;

void progress_cb(lv_timer_t* /*t*/) {
    if (!g_bar) return;
    // 1% ogni 40 ms -> pieno in 4 s circa, coerente con il tempo di boot reale.
    g_progress += 1;
    if (g_progress > 100) g_progress = 100;
    lv_bar_set_value(g_bar, g_progress, LV_ANIM_ON);
}
}  // namespace

void show() {
    if (!lvgl_port_lock(-1)) {
        ESP_LOGE(TAG, "lvgl_port_lock fallito");
        return;
    }

    g_screen = lv_obj_create(nullptr);
    lv_obj_remove_style_all(g_screen);
    lv_obj_set_size(g_screen, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(g_screen, lv_color_hex(0x0B0B10), 0);
    lv_obj_set_style_bg_opa(g_screen, LV_OPA_COVER, 0);

    /* ----------- Fascia tricolore in alto (stile GT / Competizione) ------ */
    lv_obj_t* stripe_bg = lv_obj_create(g_screen);
    lv_obj_remove_style_all(stripe_bg);
    lv_obj_set_size(stripe_bg, LV_PCT(60), 6);
    lv_obj_align(stripe_bg, LV_ALIGN_TOP_MID, 0, 70);
    lv_obj_set_flex_flow(stripe_bg, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_all(stripe_bg, 0, 0);
    lv_obj_set_style_pad_column(stripe_bg, 0, 0);

    auto add_stripe = [&](uint32_t color) {
        lv_obj_t* s = lv_obj_create(stripe_bg);
        lv_obj_remove_style_all(s);
        lv_obj_set_flex_grow(s, 1);
        lv_obj_set_height(s, LV_PCT(100));
        lv_obj_set_style_bg_color(s, lv_color_hex(color), 0);
        lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
    };
    add_stripe(0x009246);  // verde Italia
    add_stripe(0xF1F1F1);  // bianco
    add_stripe(0xCE2B37);  // rosso Italia

    /* ----------- Cerchio centrale con "595" ------------------------------ */
    lv_obj_t* badge = lv_obj_create(g_screen);
    lv_obj_remove_style_all(badge);
    lv_obj_set_size(badge, 240, 240);
    lv_obj_align(badge, LV_ALIGN_CENTER, 0, -30);
    lv_obj_set_style_radius(badge, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(badge, lv_color_hex(0x14141A), 0);
    lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(badge, 4, 0);
    lv_obj_set_style_border_color(badge, lv_color_hex(0xCC3030), 0);

    lv_obj_t* badge_num = lv_label_create(badge);
    lv_label_set_text(badge_num, "595");
    lv_obj_set_style_text_color(badge_num, lv_color_hex(0xF8F8F8), 0);
    lv_obj_set_style_text_font(badge_num, &lv_font_montserrat_48, 0);
    lv_obj_center(badge_num);

    lv_obj_t* badge_scorp = lv_label_create(badge);
    lv_label_set_text(badge_scorp, "SCORPIONE");
    lv_obj_set_style_text_color(badge_scorp, lv_color_hex(0xCC3030), 0);
    lv_obj_set_style_text_font(badge_scorp, &lv_font_montserrat_14, 0);
    lv_obj_align(badge_scorp, LV_ALIGN_CENTER, 0, 40);

    /* Arco rotante attorno al badge ------------------------------------- */
    lv_obj_t* spinner = lv_spinner_create(g_screen, 1200, 60);
    lv_obj_set_size(spinner, 280, 280);
    lv_obj_align(spinner, LV_ALIGN_CENTER, 0, -30);
    lv_obj_set_style_arc_color(spinner, lv_color_hex(0x33333A), LV_PART_MAIN);
    lv_obj_set_style_arc_color(spinner, lv_color_hex(0xCC3030), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(spinner, 6, LV_PART_MAIN);
    lv_obj_set_style_arc_width(spinner, 6, LV_PART_INDICATOR);

    /* ----------- Titolo in basso ---------------------------------------- */
    lv_obj_t* title = lv_label_create(g_screen);
    lv_label_set_text(title, "ABARTH");
    lv_obj_set_style_text_color(title, lv_color_hex(0xCC3030), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_48, 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 130);

    lv_obj_t* subtitle = lv_label_create(g_screen);
    lv_label_set_text(subtitle, "DASHBOARD 595 COMPETIZIONE");
    lv_obj_set_style_text_color(subtitle, lv_color_hex(0x9A9AA6), 0);
    lv_obj_set_style_text_font(subtitle, &lv_font_montserrat_16, 0);
    lv_obj_align(subtitle, LV_ALIGN_CENTER, 0, 180);

    /* ----------- Blocco info build / sistema (in alto a sinistra / destra) */
    lv_obj_t* info_l = lv_label_create(g_screen);
    lv_label_set_text_fmt(
        info_l,
        "Firmware: abarth-dashboard\n"
        "Build: %s %s\n"
        "LVGL: %d.%d.%d",
        __DATE__, __TIME__,
        LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);
    lv_obj_set_style_text_color(info_l, lv_color_hex(0x6A6A74), 0);
    lv_obj_set_style_text_font(info_l, &lv_font_montserrat_14, 0);
    lv_obj_align(info_l, LV_ALIGN_TOP_LEFT, 24, 24);

    lv_obj_t* info_r = lv_label_create(g_screen);
    const size_t heap_kb  = esp_get_free_heap_size() / 1024;
    const size_t psram_kb = heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024;
    lv_label_set_text_fmt(
        info_r,
        "Board:   Guition JC1060P470C\n"
        "SoC:     ESP32-P4 @ 360 MHz\n"
        "Heap:    %u KB  PSRAM: %u KB",
        static_cast<unsigned>(heap_kb),
        static_cast<unsigned>(psram_kb));
    lv_obj_set_style_text_color(info_r, lv_color_hex(0x6A6A74), 0);
    lv_obj_set_style_text_font(info_r, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(info_r, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(info_r, LV_ALIGN_TOP_RIGHT, -24, 24);

    /* ----------- Barra di caricamento ----------------------------------- */
    g_bar = lv_bar_create(g_screen);
    lv_obj_set_size(g_bar, 420, 6);
    lv_obj_align(g_bar, LV_ALIGN_BOTTOM_MID, 0, -80);
    lv_obj_set_style_bg_color(g_bar, lv_color_hex(0x22222A), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(g_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(g_bar, 3, LV_PART_MAIN);
    lv_obj_set_style_bg_color(g_bar, lv_color_hex(0xCC3030), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(g_bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(g_bar, 3, LV_PART_INDICATOR);
    lv_bar_set_range(g_bar, 0, 100);
    lv_bar_set_value(g_bar, 0, LV_ANIM_OFF);

    g_status_label = lv_label_create(g_screen);
    lv_label_set_text(g_status_label, "Inizializzazione...");
    lv_obj_set_style_text_color(g_status_label, lv_color_hex(0xDEDEDE), 0);
    lv_obj_set_style_text_font(g_status_label, &lv_font_montserrat_14, 0);
    lv_obj_align(g_status_label, LV_ALIGN_BOTTOM_MID, 0, -50);

    /* Attiva lo screen e parti col timer di avanzamento */
    lv_scr_load(g_screen);
    g_progress = 0;
    g_progress_tmr = lv_timer_create(progress_cb, 40, nullptr);

    lvgl_port_unlock();
}

void setStatus(const char* text) {
    if (!text) return;
    if (!g_status_label) return;
    if (!lvgl_port_lock(-1)) return;
    lv_label_set_text(g_status_label, text);
    lvgl_port_unlock();
}

void dismiss() {
    if (!lvgl_port_lock(-1)) return;
    if (g_progress_tmr) {
        lv_timer_del(g_progress_tmr);
        g_progress_tmr = nullptr;
    }
    if (g_bar) {
        lv_bar_set_value(g_bar, 100, LV_ANIM_OFF);
    }
    if (g_screen) {
        lv_obj_del(g_screen);
        g_screen = nullptr;
        g_bar = nullptr;
        g_status_label = nullptr;
    }
    lvgl_port_unlock();
}

}  // namespace abarth::ui::splash
