#include "ui/settings_tab.h"

#include <Arduino.h>
#include <esp_log.h>
#include <esp_system.h>
#include <esp_chip_info.h>
#include <esp_heap_caps.h>
#include <lvgl.h>
#include <sys/time.h>
#include <time.h>

#include <cstdio>
#include <cstring>

#include "config/settings.h"
#include "display/lvgl_v8_port.h"

namespace abarth::ui::settings_tab {

namespace {
constexpr const char* TAG = "settings_ui";

lv_obj_t* g_lbl_brightness_value = nullptr;
lv_obj_t* g_lbl_clock            = nullptr;
lv_obj_t* g_lbl_sysinfo          = nullptr;

/* --- Modal "Test touch" --------------------------------------------------- */
lv_obj_t* g_touch_modal = nullptr;
lv_obj_t* g_touch_dot   = nullptr;

void touch_modal_event_cb(lv_event_t* e) {
    const lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSING || code == LV_EVENT_PRESSED) {
        lv_indev_t* indev = lv_indev_get_act();
        if (!indev || !g_touch_dot) return;
        lv_point_t p;
        lv_indev_get_point(indev, &p);
        lv_obj_set_pos(g_touch_dot,
                       p.x - lv_obj_get_width(g_touch_dot) / 2,
                       p.y - lv_obj_get_height(g_touch_dot) / 2);
    }
}

void close_touch_modal_cb(lv_event_t* /*e*/) {
    if (!g_touch_modal) return;
    lv_obj_del(g_touch_modal);
    g_touch_modal = nullptr;
    g_touch_dot   = nullptr;
}

void open_touch_modal_cb(lv_event_t* /*e*/) {
    if (g_touch_modal) return;
    g_touch_modal = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(g_touch_modal);
    lv_obj_set_size(g_touch_modal, LV_HOR_RES, LV_VER_RES);
    lv_obj_align(g_touch_modal, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(g_touch_modal, lv_color_hex(0x05050A), 0);
    lv_obj_set_style_bg_opa(g_touch_modal, LV_OPA_COVER, 0);
    lv_obj_clear_flag(g_touch_modal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(g_touch_modal, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* hint = lv_label_create(g_touch_modal);
    lv_label_set_text(hint, "Tocca lo schermo per testare il touch.\nPremi 'Chiudi' per tornare indietro.");
    lv_obj_set_style_text_color(hint, lv_color_hex(0xDEDEDE), 0);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 30);

    lv_obj_t* btn = lv_btn_create(g_touch_modal);
    lv_obj_align(btn, LV_ALIGN_TOP_RIGHT, -20, 20);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0xCC3030), 0);
    lv_obj_t* btn_lbl = lv_label_create(btn);
    lv_label_set_text(btn_lbl, "Chiudi");
    lv_obj_set_style_text_color(btn_lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_add_event_cb(btn, close_touch_modal_cb, LV_EVENT_CLICKED, nullptr);

    g_touch_dot = lv_obj_create(g_touch_modal);
    lv_obj_remove_style_all(g_touch_dot);
    lv_obj_set_size(g_touch_dot, 60, 60);
    lv_obj_set_style_radius(g_touch_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(g_touch_dot, lv_color_hex(0xCC3030), 0);
    lv_obj_set_style_bg_opa(g_touch_dot, LV_OPA_80, 0);
    lv_obj_set_style_border_color(g_touch_dot, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_width(g_touch_dot, 2, 0);
    lv_obj_clear_flag(g_touch_dot, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(g_touch_dot, LV_ALIGN_CENTER, 0, 0);

    lv_obj_add_event_cb(g_touch_modal, touch_modal_event_cb, LV_EVENT_PRESSING, nullptr);
    lv_obj_add_event_cb(g_touch_modal, touch_modal_event_cb, LV_EVENT_PRESSED, nullptr);
}

/* --- Slider luminosita' --------------------------------------------------- */

void brightness_slider_cb(lv_event_t* e) {
    lv_obj_t* slider = lv_event_get_target(e);
    int32_t val = lv_slider_get_value(slider);
    if (val < 10) val = 10;
    if (val > 100) val = 100;
    settings::setBrightness(static_cast<uint8_t>(val));
    if (g_lbl_brightness_value) {
        char buf[8];
        std::snprintf(buf, sizeof(buf), "%ld%%", static_cast<long>(val));
        lv_label_set_text(g_lbl_brightness_value, buf);
    }
}

/* --- Unita' di misura ----------------------------------------------------- */

void temp_unit_btnm_cb(lv_event_t* e) {
    lv_obj_t* btnm = lv_event_get_target(e);
    uint16_t idx = lv_btnmatrix_get_selected_btn(btnm);
    settings::setTempUnit(idx == 1 ? settings::TempUnit::Fahrenheit : settings::TempUnit::Celsius);
}

void speed_unit_btnm_cb(lv_event_t* e) {
    lv_obj_t* btnm = lv_event_get_target(e);
    uint16_t idx = lv_btnmatrix_get_selected_btn(btnm);
    settings::setSpeedUnit(idx == 1 ? settings::SpeedUnit::Mph : settings::SpeedUnit::KmH);
}

/* --- Data/ora ------------------------------------------------------------- */

lv_obj_t* g_roller_year   = nullptr;
lv_obj_t* g_roller_month  = nullptr;
lv_obj_t* g_roller_day    = nullptr;
lv_obj_t* g_roller_hour   = nullptr;
lv_obj_t* g_roller_minute = nullptr;

void set_time_cb(lv_event_t* /*e*/) {
    if (!g_roller_year) return;
    struct tm t{};
    t.tm_year  = 2024 + static_cast<int>(lv_roller_get_selected(g_roller_year));
    t.tm_mon   = static_cast<int>(lv_roller_get_selected(g_roller_month));
    t.tm_mday  = 1 + static_cast<int>(lv_roller_get_selected(g_roller_day));
    t.tm_hour  = static_cast<int>(lv_roller_get_selected(g_roller_hour));
    t.tm_min   = static_cast<int>(lv_roller_get_selected(g_roller_minute));
    t.tm_sec   = 0;
    t.tm_isdst = -1;
    time_t epoch = mktime(&t);
    if (epoch < 0) {
        ESP_LOGE(TAG, "mktime fallito");
        return;
    }
    struct timeval tv{epoch, 0};
    settimeofday(&tv, nullptr);
    ESP_LOGI(TAG, "orologio impostato a %ld", static_cast<long>(epoch));
}

/* --- Reboot --------------------------------------------------------------- */

void reboot_cb(lv_event_t* /*e*/) {
    ESP_LOGI(TAG, "reboot richiesto da UI");
    // Breve delay per far propagare il log e rilasciare NVS.
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_restart();
}

/* --- Helpers ------------------------------------------------------------- */

lv_obj_t* make_section(lv_obj_t* parent, const char* title) {
    lv_obj_t* sec = lv_obj_create(parent);
    lv_obj_set_width(sec, LV_PCT(100));
    lv_obj_set_height(sec, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(sec, lv_color_hex(0x14141A), 0);
    lv_obj_set_style_bg_opa(sec, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(sec, lv_color_hex(0x33333A), 0);
    lv_obj_set_style_border_width(sec, 1, 0);
    lv_obj_set_style_radius(sec, 10, 0);
    lv_obj_set_style_pad_all(sec, 14, 0);
    lv_obj_set_style_pad_row(sec, 10, 0);
    lv_obj_set_flex_flow(sec, LV_FLEX_FLOW_COLUMN);

    lv_obj_t* h = lv_label_create(sec);
    lv_label_set_text(h, title);
    lv_obj_set_style_text_color(h, lv_color_hex(0xCC3030), 0);
    lv_obj_set_style_text_font(h, &lv_font_montserrat_18, 0);
    return sec;
}

}  // namespace

void build(lv_obj_t* tab) {
    lv_obj_set_style_pad_all(tab, 16, 0);
    lv_obj_set_style_pad_row(tab, 14, 0);
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_COLUMN);

    const auto& s = settings::current();

    /* --- Luminosita' ----------------------------------------------------- */
    lv_obj_t* sec_bl = make_section(tab, "Luminosita'");
    lv_obj_t* row    = lv_obj_create(sec_bl);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 16, 0);

    lv_obj_t* slider = lv_slider_create(row);
    lv_obj_set_width(slider, LV_PCT(80));
    lv_slider_set_range(slider, 10, 100);
    lv_slider_set_value(slider, s.brightness, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0x22222A), LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0xCC3030), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0xF8F8F8), LV_PART_KNOB);
    lv_obj_add_event_cb(slider, brightness_slider_cb, LV_EVENT_VALUE_CHANGED, nullptr);

    g_lbl_brightness_value = lv_label_create(row);
    lv_obj_set_style_text_color(g_lbl_brightness_value, lv_color_hex(0xF8F8F8), 0);
    lv_obj_set_style_text_font(g_lbl_brightness_value, &lv_font_montserrat_20, 0);
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%u%%", static_cast<unsigned>(s.brightness));
    lv_label_set_text(g_lbl_brightness_value, buf);

    /* --- Unita' ---------------------------------------------------------- */
    lv_obj_t* sec_u = make_section(tab, "Unita' di misura");

    static const char* temp_opts[] = {"C", "F", ""};
    lv_obj_t* btnm_t = lv_btnmatrix_create(sec_u);
    lv_obj_set_size(btnm_t, LV_PCT(100), 50);
    lv_btnmatrix_set_map(btnm_t, temp_opts);
    lv_btnmatrix_set_one_checked(btnm_t, true);
    lv_btnmatrix_set_btn_ctrl_all(btnm_t, LV_BTNMATRIX_CTRL_CHECKABLE);
    lv_btnmatrix_set_btn_ctrl(btnm_t,
                              s.temp_unit == settings::TempUnit::Fahrenheit ? 1 : 0,
                              LV_BTNMATRIX_CTRL_CHECKED);
    lv_obj_set_style_bg_color(btnm_t, lv_color_hex(0x14141A), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btnm_t, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(btnm_t, lv_color_hex(0x22222A), LV_PART_ITEMS);
    lv_obj_set_style_bg_color(btnm_t, lv_color_hex(0xCC3030), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(btnm_t, lv_color_hex(0xF8F8F8), LV_PART_ITEMS);
    lv_obj_add_event_cb(btnm_t, temp_unit_btnm_cb, LV_EVENT_VALUE_CHANGED, nullptr);

    static const char* speed_opts[] = {"km/h", "mph", ""};
    lv_obj_t* btnm_s = lv_btnmatrix_create(sec_u);
    lv_obj_set_size(btnm_s, LV_PCT(100), 50);
    lv_btnmatrix_set_map(btnm_s, speed_opts);
    lv_btnmatrix_set_one_checked(btnm_s, true);
    lv_btnmatrix_set_btn_ctrl_all(btnm_s, LV_BTNMATRIX_CTRL_CHECKABLE);
    lv_btnmatrix_set_btn_ctrl(btnm_s,
                              s.speed_unit == settings::SpeedUnit::Mph ? 1 : 0,
                              LV_BTNMATRIX_CTRL_CHECKED);
    lv_obj_set_style_bg_color(btnm_s, lv_color_hex(0x14141A), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btnm_s, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(btnm_s, lv_color_hex(0x22222A), LV_PART_ITEMS);
    lv_obj_set_style_bg_color(btnm_s, lv_color_hex(0xCC3030), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(btnm_s, lv_color_hex(0xF8F8F8), LV_PART_ITEMS);
    lv_obj_add_event_cb(btnm_s, speed_unit_btnm_cb, LV_EVENT_VALUE_CHANGED, nullptr);

    /* --- Orologio --------------------------------------------------------- */
    lv_obj_t* sec_c = make_section(tab, "Data e ora");
    g_lbl_clock = lv_label_create(sec_c);
    lv_obj_set_style_text_color(g_lbl_clock, lv_color_hex(0xF8F8F8), 0);
    lv_obj_set_style_text_font(g_lbl_clock, &lv_font_montserrat_24, 0);
    lv_label_set_text(g_lbl_clock, "--:--");

    lv_obj_t* rollers = lv_obj_create(sec_c);
    lv_obj_remove_style_all(rollers);
    lv_obj_set_width(rollers, LV_PCT(100));
    lv_obj_set_height(rollers, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(rollers, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(rollers, 8, 0);

    auto make_roller = [&](const char* options, int initial) {
        lv_obj_t* r = lv_roller_create(rollers);
        lv_roller_set_options(r, options, LV_ROLLER_MODE_NORMAL);
        lv_roller_set_visible_row_count(r, 3);
        lv_roller_set_selected(r, initial, LV_ANIM_OFF);
        lv_obj_set_flex_grow(r, 1);
        lv_obj_set_style_bg_color(r, lv_color_hex(0x22222A), LV_PART_MAIN);
        lv_obj_set_style_text_color(r, lv_color_hex(0xF8F8F8), LV_PART_MAIN);
        lv_obj_set_style_bg_color(r, lv_color_hex(0xCC3030), LV_PART_SELECTED);
        return r;
    };

    // Default: l'orologio parte dall'epoch 0; prendiamo 2025/01/01 00:00 come default UI.
    g_roller_year   = make_roller("2024\n2025\n2026\n2027\n2028\n2029\n2030", 1);
    g_roller_month  = make_roller("Gen\nFeb\nMar\nApr\nMag\nGiu\nLug\nAgo\nSet\nOtt\nNov\nDic", 0);
    {
        char days[32 * 3] = {0};
        size_t off = 0;
        for (int i = 1; i <= 31; ++i) {
            off += std::snprintf(days + off, sizeof(days) - off,
                                 i == 31 ? "%02d" : "%02d\n", i);
        }
        g_roller_day = make_roller(days, 0);
    }
    {
        char hrs[24 * 3] = {0};
        size_t off = 0;
        for (int i = 0; i < 24; ++i) {
            off += std::snprintf(hrs + off, sizeof(hrs) - off,
                                 i == 23 ? "%02d" : "%02d\n", i);
        }
        g_roller_hour = make_roller(hrs, 0);
    }
    {
        char mns[60 * 3] = {0};
        size_t off = 0;
        for (int i = 0; i < 60; ++i) {
            off += std::snprintf(mns + off, sizeof(mns) - off,
                                 i == 59 ? "%02d" : "%02d\n", i);
        }
        g_roller_minute = make_roller(mns, 0);
    }

    lv_obj_t* btn_set = lv_btn_create(sec_c);
    lv_obj_set_width(btn_set, LV_PCT(100));
    lv_obj_set_style_bg_color(btn_set, lv_color_hex(0xCC3030), 0);
    lv_obj_t* btn_set_lbl = lv_label_create(btn_set);
    lv_label_set_text(btn_set_lbl, "Imposta");
    lv_obj_set_style_text_color(btn_set_lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(btn_set_lbl);
    lv_obj_add_event_cb(btn_set, set_time_cb, LV_EVENT_CLICKED, nullptr);

    /* --- Info sistema ---------------------------------------------------- */
    lv_obj_t* sec_i = make_section(tab, "Info sistema");
    g_lbl_sysinfo = lv_label_create(sec_i);
    lv_obj_set_style_text_color(g_lbl_sysinfo, lv_color_hex(0xDEDEDE), 0);
    lv_obj_set_style_text_font(g_lbl_sysinfo, &lv_font_montserrat_14, 0);
    lv_label_set_text(g_lbl_sysinfo, "...");

    /* --- Azioni ---------------------------------------------------------- */
    lv_obj_t* sec_a = make_section(tab, "Azioni");

    lv_obj_t* btn_touch = lv_btn_create(sec_a);
    lv_obj_set_width(btn_touch, LV_PCT(100));
    lv_obj_set_style_bg_color(btn_touch, lv_color_hex(0x33333A), 0);
    lv_obj_t* lbl_touch = lv_label_create(btn_touch);
    lv_label_set_text(lbl_touch, "Test touch");
    lv_obj_set_style_text_color(lbl_touch, lv_color_hex(0xF8F8F8), 0);
    lv_obj_center(lbl_touch);
    lv_obj_add_event_cb(btn_touch, open_touch_modal_cb, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* btn_reboot = lv_btn_create(sec_a);
    lv_obj_set_width(btn_reboot, LV_PCT(100));
    lv_obj_set_style_bg_color(btn_reboot, lv_color_hex(0xCC3030), 0);
    lv_obj_t* lbl_rb = lv_label_create(btn_reboot);
    lv_label_set_text(lbl_rb, "Riavvia dispositivo");
    lv_obj_set_style_text_color(lbl_rb, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(lbl_rb);
    lv_obj_add_event_cb(btn_reboot, reboot_cb, LV_EVENT_CLICKED, nullptr);
}

void tick() {
    // Il tick viene chiamato dal timer UI principale (10 Hz). Qui rinfreschiamo
    // solo campi che cambiano di secondo in secondo, evitando di generare
    // pressione inutile su heap LVGL.
    static int64_t last_clock_ms   = 0;
    static int64_t last_sysinfo_ms = 0;
    const int64_t  now_ms          = esp_timer_get_time() / 1000;

    // Orologio: aggiorna 1 volta al secondo.
    if (g_lbl_clock && (now_ms - last_clock_ms) >= 1000) {
        last_clock_ms = now_ms;
        time_t now = time(nullptr);
        struct tm ti;
        localtime_r(&now, &ti);
        char buf[32];
        if (ti.tm_year + 1900 < 2024) {
            std::snprintf(buf, sizeof(buf),
                          "Non impostata (uptime %lu s)",
                          static_cast<unsigned long>(now_ms / 1000));
        } else {
            std::snprintf(buf, sizeof(buf),
                          "%04d/%02d/%02d %02d:%02d",
                          ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday,
                          ti.tm_hour, ti.tm_min);
        }
        lv_label_set_text(g_lbl_clock, buf);
    }

    // Info sistema: aggiorna ogni 2 secondi (lettura heap/PSRAM non e'
    // gratuita e il dato e' visibile solo in tab Impostazioni).
    if (g_lbl_sysinfo && (now_ms - last_sysinfo_ms) >= 2000) {
        last_sysinfo_ms = now_ms;
        char buf[256];
        const size_t free_heap = esp_get_free_heap_size();
        const size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        const uint64_t uptime_s = now_ms / 1000;
        std::snprintf(
            buf, sizeof(buf),
            "Firmware:  abarth-dashboard %s %s\n"
            "Target:    ESP32-P4 + ESP32-C6 (BT via hosted)\n"
            "Heap free: %u KB\n"
            "PSRAM free: %u KB\n"
            "Uptime:    %lluh %llum %llus",
            __DATE__, __TIME__,
            static_cast<unsigned>(free_heap / 1024),
            static_cast<unsigned>(free_psram / 1024),
            (uptime_s / 3600),
            (uptime_s / 60) % 60,
            uptime_s % 60);
        lv_label_set_text(g_lbl_sysinfo, buf);
    }
}

}  // namespace abarth::ui::settings_tab
