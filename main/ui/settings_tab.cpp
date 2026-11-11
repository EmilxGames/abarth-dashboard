#include "ui/settings_tab.h"
#include "ui/ui.h"

#include <esp_log.h>
#include <esp_system.h>
#include <esp_chip_info.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <esp_idf_version.h>
#include <lvgl.h>
#include <sys/time.h>
#include <time.h>
#include <cstdio>
#include <cstring>
#include <vector>
#include <string>

#include "config/alert_manager.h"
#include "config/settings.h"
#include "config/warmup_manager.h"
#include "display/lvgl_v8_port.h"
#include "obd/obd_manager.h"
#include "can/can_manager.h"
#include "wifi_manager.h"
#include "obd/ble_stream.h"
#include "storage/rtc_manager.h"
#include "storage/sd_card_manager.h"
#include "storage/ota_updater.h"
#include "storage/web_server.h"
#include <sys/stat.h>
#include "esp_sntp.h"

namespace abarth::ui::settings_tab {

namespace {
constexpr const char* TAG = "settings_ui";

// Stili globali tab impostazioni
static bool g_styles_inited = false;
static lv_style_t style_card_group;
static lv_style_t style_menu_card;
static lv_style_t style_menu_card_pressed;

// Puntatori ad elementi rinfrescati da tick()
lv_obj_t* g_lbl_brightness_value = nullptr;
lv_obj_t* g_lbl_sysinfo          = nullptr;
lv_obj_t* g_lbl_obd_status       = nullptr;
lv_obj_t* g_lbl_wifi_status      = nullptr;
lv_obj_t* g_lbl_ble_status       = nullptr;
lv_obj_t* g_lbl_can1_diag        = nullptr;
lv_obj_t* g_lbl_can2_diag        = nullptr;
lv_obj_t* g_lbl_counters_info    = nullptr;
lv_obj_t* g_lbl_rtc_info         = nullptr;
lv_obj_t* g_lbl_rtc_time_display = nullptr;
lv_obj_t* g_lbl_stat_device      = nullptr;
lv_obj_t* g_lbl_stat_state       = nullptr;
lv_obj_t* g_lbl_stat_ok          = nullptr;
lv_obj_t* g_lbl_stat_err         = nullptr;
lv_obj_t* g_lbl_stat_lasterr     = nullptr;
lv_obj_t* g_lbl_rtc_date_display = nullptr;
lv_obj_t* g_lbl_rtc_hour_val     = nullptr;
lv_obj_t* g_lbl_rtc_min_val      = nullptr;
lv_obj_t* g_lbl_rtc_day_val      = nullptr;
lv_obj_t* g_lbl_rtc_month_val    = nullptr;
lv_obj_t* g_lbl_rtc_year_val     = nullptr;
lv_obj_t* g_lbl_menu_time        = nullptr;
lv_obj_t* g_lbl_menu_date        = nullptr;
lv_obj_t* g_lbl_detail_time      = nullptr;
lv_obj_t* g_lbl_detail_date      = nullptr;

// Elementi di navigazione smartphone
lv_obj_t* g_menu_page            = nullptr;
lv_obj_t* g_detail_page          = nullptr;
lv_obj_t* g_detail_badge         = nullptr;
lv_obj_t* g_detail_badge_ico     = nullptr;
lv_obj_t* g_detail_title_lbl     = nullptr;
lv_obj_t* g_detail_sub_lbl       = nullptr;
lv_obj_t* g_content_panel        = nullptr;
lv_obj_t* g_menu_grid            = nullptr;
static uint8_t g_current_cat     = 0;
static uint8_t g_subcat_metric   = 0;

// Definizione metadati delle 12 categorie
struct CategoryMeta {
    const char* icon;
    const char* title;
    const char* subtitle;
    uint32_t    badge_color;
};

static const CategoryMeta g_categories[13] = {
    {LV_SYMBOL_WIFI,     "Connettivita' & OBD",    "WiFi, Bluetooth BLE, dongle e adattatore OBD-II",  0x0284C7},
    {LV_SYMBOL_IMAGE,    "Temi Grafici",          "Colori accenti, stili lancette e indicatori",       0xD97706},
    {LV_SYMBOL_SHUFFLE,  "Sfondi & Texture",      "Carbonio, Scorpione, Tricolore ed Effetto Asfalto",  0x7C3AED},
    {LV_SYMBOL_EYE_OPEN, "Display & Schermo",     "Luminosita' retroilluminazione e orientamento",      0x059669},
    {LV_SYMBOL_LIST,     "Unita' di Misura",      "Gradi C / F, velocita' km/h o mph, bar / kPa",      0xDC2626},
    {LV_SYMBOL_SETTINGS, "Test & Simulazione",    "Simulatore dinamico giri, turbo e test touch",      0xEA580C},
    {LV_SYMBOL_LIST,     "Personalizza Manometri","Selezione parametri per quadranti e barre",         0x0D9488},
    {LV_SYMBOL_WARNING,  "Diagnostica ECU",       "Scansione codici errore DTC e cancellazione",       0xE11D48},
    {LV_SYMBOL_POWER,    "Info Sistema & Stato",  "Stato OBD/ECU, ESP32-P4 SoC, diagnostica e riavvio", 0x4F46E5},
    {LV_SYMBOL_REFRESH,  "Data, Ora & RTC",       "Orologio hardware, regolazione orario e batteria",  0x0284C7},
    {LV_SYMBOL_SD_CARD,  "Memoria MicroSD",       "Storage locale, log telemetria, asset e test",      0x10B981},
    {LV_SYMBOL_BELL,     "Avvisi & Alert",        "Soglie allarmi, motore freddo e warmup termico",    0xCC3030},
    {LV_SYMBOL_FILE,     "Copyright & Licenza",   "Diritti d'autore, titolarita' e note legali",       0x0D9488}
};
constexpr size_t NUM_CATEGORIES = sizeof(g_categories) / sizeof(g_categories[0]);

// Modalita' e dialoghi
lv_obj_t* g_modal_bg             = nullptr;
lv_obj_t* g_modal_content        = nullptr;
lv_obj_t* g_modal_body           = nullptr;
lv_obj_t* g_pw_ta                = nullptr;
static bool g_scan_in_progress   = false;

// Touch test modal elements
lv_obj_t* g_touch_test_modal    = nullptr;
lv_obj_t* g_touch_dot           = nullptr;
lv_obj_t* g_touch_coords_lbl    = nullptr;

void ensure_styles() {
    if (g_styles_inited) return;
    g_styles_inited = true;

    // Card menu categorie
    lv_style_init(&style_menu_card);
    lv_style_set_bg_color(&style_menu_card, lv_color_hex(0x14141E));
    lv_style_set_bg_opa(&style_menu_card, LV_OPA_COVER);
    lv_style_set_border_color(&style_menu_card, lv_color_hex(0x252538));
    lv_style_set_border_width(&style_menu_card, 1);
    lv_style_set_radius(&style_menu_card, 12);
    lv_style_set_pad_hor(&style_menu_card, 12);
    lv_style_set_pad_ver(&style_menu_card, 8);
    lv_style_set_shadow_width(&style_menu_card, 0);
    lv_style_set_anim_duration(&style_menu_card, 0);

    // Card menu categorie quando premuta
    lv_style_init(&style_menu_card_pressed);
    lv_style_set_bg_color(&style_menu_card_pressed, lv_color_hex(0x28283E));
    lv_style_set_border_color(&style_menu_card_pressed, lv_color_hex(0xCC3030));
    lv_style_set_border_width(&style_menu_card_pressed, 1);
    lv_style_set_shadow_width(&style_menu_card_pressed, 0);
    lv_style_set_anim_duration(&style_menu_card_pressed, 0);

    // Gruppo Card stile Smartphone (iOS/Android)
    lv_style_init(&style_card_group);
    lv_style_set_bg_color(&style_card_group, lv_color_hex(0x161622));
    lv_style_set_bg_opa(&style_card_group, LV_OPA_COVER);
    lv_style_set_border_color(&style_card_group, lv_color_hex(0x282836));
    lv_style_set_border_width(&style_card_group, 1);
    lv_style_set_radius(&style_card_group, 14);
    lv_style_set_pad_all(&style_card_group, 16);
    lv_style_set_pad_row(&style_card_group, 12);
    lv_style_set_shadow_width(&style_card_group, 0);
    lv_style_set_anim_duration(&style_card_group, 0);
}

void close_modal(lv_event_t* = nullptr) {
    if (g_modal_bg) {
        lv_obj_del_async(g_modal_bg);
        g_modal_bg = nullptr;
        g_modal_content = nullptr;
        g_modal_body = nullptr;
        g_pw_ta = nullptr;
    }
}

lv_obj_t* create_modal(const char* title_text) {
    if (g_modal_bg) {
        lv_obj_del(g_modal_bg);
        g_modal_bg = nullptr;
    }

    g_modal_bg = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(g_modal_bg);
    lv_obj_set_size(g_modal_bg, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(g_modal_bg, lv_color_hex(0x040408), 0);
    lv_obj_set_style_bg_opa(g_modal_bg, LV_OPA_80, 0);
    lv_obj_add_flag(g_modal_bg, LV_OBJ_FLAG_CLICKABLE);

    g_modal_content = lv_obj_create(g_modal_bg);
    lv_obj_set_size(g_modal_content, LV_PCT(82), LV_PCT(82));
    lv_obj_center(g_modal_content);
    lv_obj_set_style_bg_color(g_modal_content, lv_color_hex(0x181824), 0);
    lv_obj_set_style_border_color(g_modal_content, lv_color_hex(0x3A3A4C), 0);
    lv_obj_set_style_border_width(g_modal_content, 2, 0);
    lv_obj_set_style_radius(g_modal_content, 16, 0);
    lv_obj_set_flex_flow(g_modal_content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(g_modal_content, 16, 0);

    lv_obj_t* header = lv_obj_create(g_modal_content);
    lv_obj_set_width(header, LV_PCT(100));
    lv_obj_set_height(header, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_remove_style(header, nullptr, LV_PART_SCROLLBAR);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* title = lv_label_create(header);
    lv_label_set_text(title, title_text);
    lv_obj_set_style_text_color(title, lv_color_hex(0xCC3030), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);

    lv_obj_t* btn_close = lv_btn_create(header);
    lv_obj_set_style_bg_color(btn_close, lv_color_hex(0x333344), 0);
    lv_obj_set_style_radius(btn_close, 8, 0);
    lv_obj_t* lbl_c = lv_label_create(btn_close);
    lv_label_set_text(lbl_c, "X");
    lv_obj_center(lbl_c);
    lv_obj_add_event_cb(btn_close, close_modal, LV_EVENT_CLICKED, nullptr);

    g_modal_body = lv_obj_create(g_modal_content);
    lv_obj_set_width(g_modal_body, LV_PCT(100));
    lv_obj_set_flex_grow(g_modal_body, 1);
    lv_obj_set_style_bg_opa(g_modal_body, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(g_modal_body, 0, 0);
    lv_obj_set_style_pad_all(g_modal_body, 0, 0);
    lv_obj_set_flex_flow(g_modal_body, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_style(g_modal_body, nullptr, LV_PART_SCROLLBAR);

    return g_modal_body;
}

// --- Modale Test Touch ---
void close_touch_test(lv_event_t* = nullptr) {
    if (g_touch_test_modal) {
        lv_obj_del_async(g_touch_test_modal);
        g_touch_test_modal = nullptr;
        g_touch_dot = nullptr;
        g_touch_coords_lbl = nullptr;
    }
}

void touch_test_event_cb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSING || code == LV_EVENT_PRESSED) {
        lv_point_t p;
        lv_indev_get_point(lv_indev_get_act(), &p);
        if (g_touch_dot) {
            lv_obj_set_pos(g_touch_dot, p.x - 25, p.y - 25);
            lv_obj_clear_flag(g_touch_dot, LV_OBJ_FLAG_HIDDEN);
        }
        if (g_touch_coords_lbl) {
            lv_label_set_text_fmt(g_touch_coords_lbl, "Touch: X = %d | Y = %d", (int)p.x, (int)p.y);
        }
    }
}

void open_touch_test_modal(lv_event_t* = nullptr) {
    close_touch_test();

    g_touch_test_modal = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(g_touch_test_modal);
    lv_obj_set_size(g_touch_test_modal, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(g_touch_test_modal, lv_color_hex(0x080810), 0);
    lv_obj_set_style_bg_opa(g_touch_test_modal, LV_OPA_COVER, 0);
    lv_obj_add_flag(g_touch_test_modal, LV_OBJ_FLAG_CLICKABLE);

    // Griglia guida touch
    lv_obj_t* hint = lv_label_create(g_touch_test_modal);
    lv_label_set_text(hint, "TEST TOUCH SCREEN - Trascina il dito sullo schermo per testare la precisione GT911");
    lv_obj_set_style_text_color(hint, lv_color_hex(0x888899), 0);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 16);

    g_touch_coords_lbl = lv_label_create(g_touch_test_modal);
    lv_label_set_text(g_touch_coords_lbl, "Touch: X = --- | Y = ---");
    lv_obj_set_style_text_color(g_touch_coords_lbl, lv_color_hex(0x00FF88), 0);
    lv_obj_set_style_text_font(g_touch_coords_lbl, &lv_font_montserrat_20, 0);
    lv_obj_align(g_touch_coords_lbl, LV_ALIGN_TOP_MID, 0, 44);

    // Pallino rosso indicatore
    g_touch_dot = lv_obj_create(g_touch_test_modal);
    lv_obj_set_size(g_touch_dot, 50, 50);
    lv_obj_set_style_radius(g_touch_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(g_touch_dot, lv_color_hex(0xFF1744), 0);
    lv_obj_set_style_border_color(g_touch_dot, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_width(g_touch_dot, 3, 0);
    lv_obj_add_flag(g_touch_dot, LV_OBJ_FLAG_HIDDEN);

    // Pulsante Chiudi
    lv_obj_t* btn_close = lv_btn_create(g_touch_test_modal);
    lv_obj_set_size(btn_close, 140, 46);
    lv_obj_align(btn_close, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_set_style_bg_color(btn_close, lv_color_hex(0xCC3030), 0);
    lv_obj_set_style_radius(btn_close, 10, 0);
    lv_obj_t* lbl_c = lv_label_create(btn_close);
    lv_label_set_text(lbl_c, "Chiudi Test");
    lv_obj_center(lbl_c);
    lv_obj_add_event_cb(btn_close, close_touch_test, LV_EVENT_CLICKED, nullptr);

    lv_obj_add_event_cb(g_touch_test_modal, touch_test_event_cb, LV_EVENT_ALL, nullptr);
}

// --- Callback WiFi & BLE ---
void wifi_connect_btn_cb(lv_event_t* e) {
    const char* ssid = static_cast<const char*>(lv_event_get_user_data(e));
    std::string pw = g_pw_ta ? lv_textarea_get_text(g_pw_ta) : "";
    ESP_LOGI(TAG, "Connessione manuale WiFi a %s", ssid);
    
    close_modal(nullptr);
    lv_obj_t* modal_load = create_modal("Connessione in corso...");
    lv_obj_t* spinner = lv_spinner_create(modal_load, 1000, 60);
    lv_obj_set_size(spinner, 60, 60);
    lv_obj_center(spinner);

    abarth::wifi::WifiManager::instance().connect(ssid, pw.c_str(), [ssid_str = std::string(ssid)](bool ok, const std::string& ip, const std::string& gw_ip){
        lvgl_port_lock(0);
        close_modal(nullptr);
        
        lv_obj_t* res_modal = create_modal("Risultato Connessione");
        lv_obj_t* lbl = lv_label_create(res_modal);
        if (ok) {
            lv_label_set_text_fmt(lbl, "Connesso con successo!\nIP: %s", ip.c_str());
            lv_obj_set_style_text_color(lbl, lv_color_hex(0x00FF00), 0);
            ESP_LOGI(TAG, "WiFi connesso. IP: %s, GW: %s", ip.c_str(), gw_ip.c_str());
            settings::setSavedWifi(ssid_str, ""); 
            abarth::obd::obd().connectWiFi(gw_ip, 35000); 
        } else {
            lv_label_set_text(lbl, "Errore di connessione.\nVerifica la password o riprova.");
            lv_obj_set_style_text_color(lbl, lv_color_hex(0xFF0000), 0);
            ESP_LOGE(TAG, "Connessione WiFi fallita");
        }
        
        lv_obj_t* btn_ok = lv_btn_create(res_modal);
        lv_obj_set_style_bg_color(btn_ok, lv_color_hex(0xCC3030), 0);
        lv_obj_set_width(btn_ok, LV_PCT(50));
        lv_obj_t* lbl_ok = lv_label_create(btn_ok);
        lv_label_set_text(lbl_ok, "OK");
        lv_obj_center(lbl_ok);
        lv_obj_add_event_cb(btn_ok, close_modal, LV_EVENT_CLICKED, nullptr);

        lvgl_port_unlock();
    });
}

void on_wifi_net_click(lv_event_t* e) {
    const char* ssid = static_cast<const char*>(lv_event_get_user_data(e));
    
    close_modal(nullptr);
    lv_obj_t* modal = create_modal("Password WiFi");

    lv_obj_t* lbl = lv_label_create(modal);
    lv_label_set_text_fmt(lbl, "Rete: %s", ssid);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);

    lv_obj_t* pwd_row = lv_obj_create(modal);
    lv_obj_set_width(pwd_row, LV_PCT(100));
    lv_obj_set_height(pwd_row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(pwd_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_bg_opa(pwd_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(pwd_row, 0, 0);
    lv_obj_set_style_pad_all(pwd_row, 0, 0);
    
    g_pw_ta = lv_textarea_create(pwd_row);
    lv_obj_set_flex_grow(g_pw_ta, 1);
    lv_textarea_set_password_mode(g_pw_ta, true);
    lv_textarea_set_one_line(g_pw_ta, true);
    
    lv_obj_t* btn_eye = lv_btn_create(pwd_row);
    lv_obj_set_style_bg_color(btn_eye, lv_color_hex(0x444455), 0);
    lv_obj_t* lbl_eye = lv_label_create(btn_eye);
    lv_label_set_text(lbl_eye, LV_SYMBOL_EYE_OPEN);
    lv_obj_add_event_cb(btn_eye, [](lv_event_t* ev){
        bool is_pwd = lv_textarea_get_password_mode(g_pw_ta);
        lv_textarea_set_password_mode(g_pw_ta, !is_pwd);
        lv_obj_t* lbl = lv_obj_get_child(lv_event_get_target(ev), 0);
        lv_label_set_text(lbl, !is_pwd ? LV_SYMBOL_EYE_OPEN : LV_SYMBOL_EYE_CLOSE);
    }, LV_EVENT_CLICKED, nullptr);
    
    lv_obj_t* kb = lv_keyboard_create(modal);
    lv_keyboard_set_textarea(kb, g_pw_ta);
    lv_obj_set_size(kb, LV_PCT(100), LV_PCT(60));

    lv_obj_t* btn_conn = lv_btn_create(modal);
    lv_obj_set_width(btn_conn, LV_PCT(100));
    lv_obj_set_style_bg_color(btn_conn, lv_color_hex(0xCC3030), 0);
    lv_obj_t* lbl_conn = lv_label_create(btn_conn);
    lv_label_set_text(lbl_conn, "Connetti");
    
    char* ssid_copy = strdup(ssid);
    lv_obj_add_event_cb(btn_conn, wifi_connect_btn_cb, LV_EVENT_CLICKED, ssid_copy);
}

bool check_already_connected() {
    if (abarth::wifi::WifiManager::instance().isConnected() || !abarth::settings::current().saved_dongle_mac.empty()) {
        close_modal(nullptr);
        lv_obj_t* modal = create_modal("Attenzione");
        lv_obj_t* lbl = lv_label_create(modal);
        lv_label_set_text(lbl, "Devi prima disconnettere la rete WiFi\no il dispositivo BLE attuale\nper avviare una nuova scansione!");
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFAAAA), 0);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
        
        lv_obj_t* btn_ok = lv_btn_create(modal);
        lv_obj_set_style_bg_color(btn_ok, lv_color_hex(0xCC3030), 0);
        lv_obj_set_width(btn_ok, LV_PCT(50));
        lv_obj_t* lbl_ok = lv_label_create(btn_ok);
        lv_label_set_text(lbl_ok, "OK");
        lv_obj_center(lbl_ok);
        lv_obj_add_event_cb(btn_ok, close_modal, LV_EVENT_CLICKED, nullptr);
        return true;
    }
    return false;
}

void wifi_scan_cb(lv_event_t* /*e*/) {
    if (g_scan_in_progress) return;
    if (check_already_connected()) return;
    g_scan_in_progress = true;

    lv_obj_t* modal = create_modal("Scansione WiFi...");
    lv_obj_t* spinner = lv_spinner_create(modal, 1000, 60);
    lv_obj_set_size(spinner, 50, 50);
    lv_obj_center(spinner);

    abarth::wifi::WifiManager::instance().startScan([](const std::vector<abarth::wifi::WifiNetwork>& nets){
        lvgl_port_lock(0);
        g_scan_in_progress = false;

        if (g_modal_body) {
            lv_obj_clean(g_modal_body);
            
            lv_obj_t* header = lv_label_create(g_modal_body);
            lv_label_set_text(header, "Reti Trovate:");
            lv_obj_set_style_text_color(header, lv_color_hex(0x00FFFF), 0);
            
            lv_obj_t* list = lv_list_create(g_modal_body);
            lv_obj_set_size(list, LV_PCT(100), LV_PCT(100));
            lv_obj_set_style_bg_color(list, lv_color_hex(0x22222A), 0);
            
            for (const auto& net : nets) {
                char buf[64];
                std::snprintf(buf, sizeof(buf), "%s (RSSI: %d)", net.ssid.c_str(), net.rssi);
                lv_obj_t* btn = lv_list_add_btn(list, LV_SYMBOL_WIFI, buf);
                lv_obj_set_style_bg_color(btn, lv_color_hex(0x14141A), 0);
                lv_obj_set_style_text_color(btn, lv_color_hex(0xFFFFFF), 0);
                char* ssid_copy = strdup(net.ssid.c_str());
                lv_obj_add_event_cb(btn, on_wifi_net_click, LV_EVENT_CLICKED, ssid_copy);
            }
        }
        lvgl_port_unlock();
    });
}

void on_ble_net_click(lv_event_t* e) {
    const char* mac = static_cast<const char*>(lv_event_get_user_data(e));
    ESP_LOGI(TAG, "Connessione manuale BLE a MAC: %s", mac);
    settings::setAutoConnect(true);
    abarth::obd::obd().connectBLE(mac);
    close_modal(nullptr);
}

void ble_scan_cb(lv_event_t* /*e*/) {
    if (g_scan_in_progress) return;
    if (check_already_connected()) return;
    g_scan_in_progress = true;

    lv_obj_t* modal = create_modal("Scansione BLE...");
    lv_obj_t* spinner = lv_spinner_create(modal, 1000, 60);
    lv_obj_set_size(spinner, 50, 50);
    lv_obj_center(spinner);

    xTaskCreatePinnedToCore([](void* arg) {
        abarth::obd::BleStream temp_ble;
        temp_ble.begin({});
        auto devs = temp_ble.scanDevices(5);

        lvgl_port_lock(0);
        g_scan_in_progress = false;

        if (g_modal_body) {
            lv_obj_clean(g_modal_body); 
            
            lv_obj_t* header = lv_label_create(g_modal_body);
            lv_label_set_text(header, "Dispositivi Bluetooth:");
            lv_obj_set_style_text_color(header, lv_color_hex(0x00FFFF), 0);
            
            lv_obj_t* list = lv_list_create(g_modal_body);
            lv_obj_set_size(list, LV_PCT(100), LV_PCT(100));
            lv_obj_set_style_bg_color(list, lv_color_hex(0x22222A), 0);
            
            for (const auto& dev : devs) {
                std::string mac = "";
                size_t p1 = dev.find('[');
                size_t p2 = dev.find(']');
                if (p1 != std::string::npos && p2 != std::string::npos) {
                    mac = dev.substr(p1+1, p2-p1-1);
                }
                
                lv_obj_t* btn = lv_list_add_btn(list, LV_SYMBOL_BLUETOOTH, dev.c_str());
                lv_obj_set_style_bg_color(btn, lv_color_hex(0x14141A), 0);
                lv_obj_set_style_text_color(btn, lv_color_hex(0xFFFFFF), 0);
                char* mac_copy = strdup(mac.c_str());
                lv_obj_add_event_cb(btn, on_ble_net_click, LV_EVENT_CLICKED, mac_copy);
            }
        }
        lvgl_port_unlock();
        vTaskDelete(nullptr);
    }, "ble_scan_th", 4096, nullptr, 1, nullptr, 1);
}

void auto_connect_sw_cb(lv_event_t* e) {
    lv_obj_t* sw = lv_event_get_target(e);
    bool state = lv_obj_has_state(sw, LV_STATE_CHECKED);
    settings::setAutoConnect(state);
}

void disconnect_wifi_cb(lv_event_t* /*e*/) {
    abarth::wifi::WifiManager::instance().disconnect();
    abarth::obd::obd().forgetDevice();
    settings::setSavedWifi("", "");
}

void disconnect_ble_cb(lv_event_t* /*e*/) {
    abarth::obd::obd().forgetDevice();
}

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

void reboot_cb(lv_event_t* /*e*/) {
    ESP_LOGI(TAG, "reboot richiesto da UI");
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_restart();
}

// --- Helper Card Group Stile Smartphone ---
lv_obj_t* make_card_group(lv_obj_t* parent, const char* title, const char* icon_symbol = nullptr) {
    lv_obj_t* card = lv_obj_create(parent);
    lv_obj_remove_style_all(card);
    lv_obj_add_style(card, &style_card_group, 0);
    lv_obj_set_width(card, LV_PCT(100));
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_style(card, nullptr, LV_PART_SCROLLBAR);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    if (title) {
        lv_obj_t* header_row = lv_obj_create(card);
        lv_obj_set_width(header_row, LV_PCT(100));
        lv_obj_set_height(header_row, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(header_row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(header_row, 0, 0);
        lv_obj_set_style_pad_all(header_row, 0, 0);
        lv_obj_set_flex_flow(header_row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(header_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(header_row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(header_row, LV_OBJ_FLAG_CLICKABLE);

        if (icon_symbol) {
            lv_obj_t* ico = lv_label_create(header_row);
            lv_label_set_text(ico, icon_symbol);
            lv_obj_set_style_text_color(ico, lv_color_hex(0xCC3030), 0);
            lv_obj_set_style_text_font(ico, &lv_font_montserrat_18, 0);
            lv_obj_set_style_pad_right(ico, 8, 0);
            lv_obj_clear_flag(ico, LV_OBJ_FLAG_CLICKABLE);
        }

        lv_obj_t* h = lv_label_create(header_row);
        lv_label_set_text(h, title);
        lv_label_set_long_mode(h, LV_LABEL_LONG_WRAP);
        lv_obj_set_flex_grow(h, 1);
        lv_obj_set_style_text_color(h, lv_color_hex(0xEEEEEE), 0);
        lv_obj_set_style_text_font(h, &lv_font_montserrat_16, 0);
        lv_obj_clear_flag(h, LV_OBJ_FLAG_CLICKABLE);
    }
    return card;
}

static lv_obj_t* make_sub_label(lv_obj_t* parent, const char* text) {
    lv_obj_t* lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(lbl, LV_PCT(100));
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xDEDEDE), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    return lbl;
}

static void style_dropdown(lv_obj_t* dd, lv_color_t border_col) {
    lv_obj_set_width(dd, LV_PCT(100));
    lv_obj_set_height(dd, 46);
    lv_obj_set_style_bg_color(dd, lv_color_hex(0x181826), 0);
    lv_obj_set_style_bg_opa(dd, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(dd, border_col, 0);
    lv_obj_set_style_border_width(dd, 2, 0);
    lv_obj_set_style_radius(dd, 10, 0);
    lv_obj_set_style_text_color(dd, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(dd, &lv_font_montserrat_14, 0);

    lv_obj_t* list = lv_dropdown_get_list(dd);
    if (list) {
        lv_obj_set_style_bg_color(list, lv_color_hex(0x181826), 0);
        lv_obj_set_style_bg_opa(list, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(list, border_col, 0);
        lv_obj_set_style_border_width(list, 2, 0);
        lv_obj_set_style_radius(list, 10, 0);
        lv_obj_set_style_text_color(list, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_font(list, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_font(list, &lv_font_montserrat_14, LV_PART_SELECTED);
    }
}

static void style_compact_dropdown(lv_obj_t* dd, lv_color_t border_col) {
    lv_obj_set_style_bg_color(dd, lv_color_hex(0x181826), 0);
    lv_obj_set_style_bg_opa(dd, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(dd, border_col, 0);
    lv_obj_set_style_border_width(dd, 1, 0);
    lv_obj_set_style_radius(dd, 8, 0);
    lv_obj_set_style_pad_hor(dd, 10, 0);
    lv_obj_set_style_pad_ver(dd, 4, 0);
    lv_obj_set_style_text_color(dd, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(dd, &lv_font_montserrat_12, 0);

    lv_obj_t* list = lv_dropdown_get_list(dd);
    if (list) {
        lv_obj_set_style_bg_color(list, lv_color_hex(0x181826), 0);
        lv_obj_set_style_bg_opa(list, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(list, border_col, 0);
        lv_obj_set_style_border_width(list, 1, 0);
        lv_obj_set_style_radius(list, 8, 0);
        lv_obj_set_style_text_color(list, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_font(list, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_font(list, &lv_font_montserrat_12, LV_PART_SELECTED);
    }
}

static void add_metric_toggle(lv_obj_t* parent, abarth::data::Metric m, const char* title, const char* subtitle) {
    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_ver(row, 8, 0);
    lv_obj_set_style_pad_hor(row, 4, 0);
    lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(row, lv_color_hex(0x222234), 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_COLUMN);

    // 1) Riga Superiore: Titolo (sinistra) + Switch (destra)
    lv_obj_t* top_row = lv_obj_create(row);
    lv_obj_remove_style_all(top_row);
    lv_obj_set_width(top_row, LV_PCT(100));
    lv_obj_set_height(top_row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(top_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* lbl = lv_label_create(top_row);
    lv_label_set_text(lbl, title);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_flex_grow(lbl, 1);

    lv_obj_t* sw = lv_switch_create(top_row);
    lv_obj_set_style_bg_color(sw, lv_color_hex(0x00CC66), (lv_style_selector_t)((uint32_t)LV_PART_INDICATOR | (uint32_t)LV_STATE_CHECKED));
    if (abarth::settings::isMetricVisible(m)) {
        lv_obj_add_state(sw, LV_STATE_CHECKED);
    }

    lv_obj_add_event_cb(sw, [](lv_event_t* e){
        uintptr_t metric_raw = (uintptr_t)lv_event_get_user_data(e);
        abarth::data::Metric target_m = static_cast<abarth::data::Metric>(metric_raw);
        bool enabled = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
        abarth::settings::setMetricVisible(target_m, enabled);
        abarth::ui::apply_metric_visibility();
    }, LV_EVENT_VALUE_CHANGED, (void*)(uintptr_t)m);

    // 2) Descrizione Sottostante (se presente)
    if (subtitle) {
        lv_obj_t* sub = lv_label_create(row);
        lv_label_set_text(sub, subtitle);
        lv_obj_set_style_text_color(sub, lv_color_hex(0x9A9AA6), 0);
        lv_obj_set_style_text_font(sub, &lv_font_montserrat_12, 0);
        lv_obj_set_style_pad_top(sub, 2, 0);
    }
}

static void add_custom_metric_toggle(lv_obj_t* parent, abarth::data::Metric m, const char* title, const char* subtitle) {
    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_ver(row, 8, 0);
    lv_obj_set_style_pad_hor(row, 4, 0);
    lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(row, lv_color_hex(0x222234), 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_COLUMN);

    lv_obj_t* top_row = lv_obj_create(row);
    lv_obj_remove_style_all(top_row);
    lv_obj_set_width(top_row, LV_PCT(100));
    lv_obj_set_height(top_row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(top_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* lbl = lv_label_create(top_row);
    lv_label_set_text(lbl, title);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_flex_grow(lbl, 1);

    lv_obj_t* sw = lv_switch_create(top_row);
    lv_obj_set_style_bg_color(sw, lv_color_hex(0x00CC66), (lv_style_selector_t)((uint32_t)LV_PART_INDICATOR | (uint32_t)LV_STATE_CHECKED));
    if (abarth::settings::isMetricInCustomTab(m)) {
        lv_obj_add_state(sw, LV_STATE_CHECKED);
    }

    lv_obj_add_event_cb(sw, [](lv_event_t* e){
        uintptr_t metric_raw = (uintptr_t)lv_event_get_user_data(e);
        abarth::data::Metric target_m = static_cast<abarth::data::Metric>(metric_raw);
        bool enabled = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
        abarth::settings::setMetricInCustomTab(target_m, enabled);
        abarth::ui::rebuild_custom_tab();
    }, LV_EVENT_VALUE_CHANGED, (void*)(uintptr_t)m);

    if (subtitle) {
        lv_obj_t* sub = lv_label_create(row);
        lv_label_set_text(sub, subtitle);
        lv_obj_set_style_text_color(sub, lv_color_hex(0x9A9AA6), 0);
        lv_obj_set_style_text_font(sub, &lv_font_montserrat_12, 0);
        lv_obj_set_style_pad_top(sub, 2, 0);
    }
}

// Helper per l'aggiornamento in tempo reale dei widget Data & Ora
static void refresh_rtc_ui() {
    auto& rtc = abarth::storage::rtc();
    time_t now = time(nullptr);
    struct tm ti;
    localtime_r(&now, &ti);

    static const char* days[] = {"Domenica", "Lunedi'", "Martedi'", "Mercoledi'", "Giovedi'", "Venerdi'", "Sabato"};
    static const char* months[] = {
        "Gennaio", "Febbraio", "Marzo", "Aprile", "Maggio", "Giugno",
        "Luglio", "Agosto", "Settembre", "Ottobre", "Novembre", "Dicembre"
    };

    if (g_lbl_menu_time) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d", ti.tm_hour, ti.tm_min, ti.tm_sec);
        lv_label_set_text(g_lbl_menu_time, buf);
    }

    if (g_lbl_menu_date) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%s, %02d %s %04d",
                      days[ti.tm_wday % 7],
                      ti.tm_mday,
                      months[ti.tm_mon % 12],
                      ti.tm_year + 1900);
        lv_label_set_text(g_lbl_menu_date, buf);
    }

    if (g_lbl_detail_time) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d", ti.tm_hour, ti.tm_min, ti.tm_sec);
        lv_label_set_text(g_lbl_detail_time, buf);
    }

    if (g_lbl_detail_date) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%02d/%02d/%04d", ti.tm_mday, ti.tm_mon + 1, ti.tm_year + 1900);
        lv_label_set_text(g_lbl_detail_date, buf);
    }

    if (g_lbl_rtc_time_display) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%02d : %02d : %02d", ti.tm_hour, ti.tm_min, ti.tm_sec);
        lv_label_set_text(g_lbl_rtc_time_display, buf);
    }

    if (g_lbl_rtc_date_display) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%s, %02d %s %04d",
                      days[ti.tm_wday % 7],
                      ti.tm_mday,
                      months[ti.tm_mon % 12],
                      ti.tm_year + 1900);
        lv_label_set_text(g_lbl_rtc_date_display, buf);
    }

    if (g_lbl_rtc_hour_val) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%02d", ti.tm_hour);
        lv_label_set_text(g_lbl_rtc_hour_val, buf);
    }

    if (g_lbl_rtc_min_val) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%02d", ti.tm_min);
        lv_label_set_text(g_lbl_rtc_min_val, buf);
    }

    if (g_lbl_rtc_day_val) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%02d", ti.tm_mday);
        lv_label_set_text(g_lbl_rtc_day_val, buf);
    }

    if (g_lbl_rtc_month_val) {
        static const char* m_short[] = {
            "Gen (01)", "Feb (02)", "Mar (03)", "Apr (04)", "Mag (05)", "Giu (06)",
            "Lug (07)", "Ago (08)", "Set (09)", "Ott (10)", "Nov (11)", "Dic (12)"
        };
        lv_label_set_text(g_lbl_rtc_month_val, m_short[ti.tm_mon % 12]);
    }

    if (g_lbl_rtc_year_val) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%04d", ti.tm_year + 1900);
        lv_label_set_text(g_lbl_rtc_year_val, buf);
    }

    if (g_lbl_rtc_info) {
        char rtc_buf[320];
        if (rtc.isAvailable()) {
            std::snprintf(rtc_buf, sizeof(rtc_buf),
                "Chip Hardware: %s  |  Indirizzo I2C: 0x%02X\n"
                "Batteria Backup CR1220: %s\n"
                "Sorgente Orologio: %s  |  Fuso: Europe/Rome (CET/CEST)",
                rtc.getChipName(),
                rtc.getI2cAddress(),
                rtc.isBatteryOk() ? "CARICA E ATTIVA (OK - ~3.0V)" : "ATTENZIONE: SCARICA O NON INSERITA",
                rtc.isNtpSynced() ? "Sincronizzato da Internet (NTP WiFi)" :
                (rtc.isTimeValid() ? "Sincronizzato da Batteria RTC" : "Inizializzazione Richiesta")
            );
        } else {
            std::snprintf(rtc_buf, sizeof(rtc_buf),
                "Chip RTC non rilevato sul bus I2C (I2C_NUM_0 SDA=7, SCL=8).\n"
                "Clock di sistema ESP32 attivo."
            );
        }
        lv_label_set_text(g_lbl_rtc_info, rtc_buf);
    }
}

// Forward declarations per navigazione e rendering
void render_category(uint8_t cat_idx, uint16_t subtab_idx = 0);
void open_category(uint8_t cat_idx, uint16_t subtab_idx = 0);
void close_category();

// --- Render Contenuto Categoria ---
void render_category(uint8_t cat_idx, uint16_t subtab_idx) {
    if (!g_content_panel) return;

    // Pulisci il pannello di destra
    lv_obj_clean(g_content_panel);
    g_lbl_brightness_value = nullptr;
    g_lbl_sysinfo          = nullptr;
    g_lbl_obd_status       = nullptr;
    g_lbl_wifi_status      = nullptr;
    g_lbl_ble_status       = nullptr;
    g_lbl_can1_diag        = nullptr;
    g_lbl_can2_diag        = nullptr;
    g_lbl_counters_info    = nullptr;
    g_lbl_rtc_info         = nullptr;
    g_lbl_rtc_time_display = nullptr;
    g_lbl_rtc_date_display = nullptr;
    g_lbl_rtc_hour_val     = nullptr;
    g_lbl_rtc_min_val      = nullptr;
    g_lbl_rtc_day_val      = nullptr;
    g_lbl_rtc_month_val    = nullptr;
    g_lbl_rtc_year_val     = nullptr;
    g_lbl_stat_device      = nullptr;
    g_lbl_stat_state       = nullptr;
    g_lbl_stat_ok          = nullptr;
    g_lbl_stat_err         = nullptr;
    g_lbl_stat_lasterr     = nullptr;

    const auto& s = settings::current();

    switch (cat_idx) {
        // === 0: CONNETTIVITA' & OBD ===
        case 0: {
            // Card 0: Modalita' di Collegamento Vettura (BLE, WiFi, CAN-Bus)
            lv_obj_t* c_mode = make_card_group(g_content_panel, "Modalita' di Collegamento Vettura", LV_SYMBOL_SETTINGS);
            make_sub_label(c_mode, "Seleziona come il display acquisisce la telemetria dall'Abarth:");

            lv_obj_t* r_m_btns = lv_obj_create(c_mode);
            lv_obj_set_width(r_m_btns, LV_PCT(100));
            lv_obj_set_style_bg_opa(r_m_btns, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(r_m_btns, 0, 0);
            lv_obj_set_style_pad_all(r_m_btns, 0, 0);
            lv_obj_set_flex_flow(r_m_btns, LV_FLEX_FLOW_ROW_WRAP);
            lv_obj_set_style_pad_gap(r_m_btns, 8, 0);

            auto make_mode_btn = [&](const char* icon, const char* title, const char* subtitle, settings::ConnectionMode m) {
                const bool is_active = (s.connection_mode == m);
                lv_obj_t* btn = lv_btn_create(r_m_btns);
                lv_obj_set_flex_grow(btn, 1);
                lv_obj_set_style_min_width(btn, 180, 0);
                lv_obj_set_height(btn, 54);
                lv_obj_set_style_radius(btn, 8, 0);
                lv_obj_set_style_border_width(btn, is_active ? 2 : 1, 0);
                lv_obj_set_style_border_color(btn, is_active ? lv_color_hex(0xCC3030) : lv_color_hex(0x383848), 0);
                lv_obj_set_style_bg_color(btn, is_active ? lv_color_hex(0x2E1618) : lv_color_hex(0x1C1C26), 0);
                lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_COLUMN);
                lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
                lv_obj_set_style_pad_all(btn, 6, 0);

                lv_obj_t* l_t = lv_label_create(btn);
                char tbuf[64];
                snprintf(tbuf, sizeof(tbuf), "%s %s%s", icon, title, is_active ? " [ATTIVO]" : "");
                lv_label_set_text(l_t, tbuf);
                lv_obj_set_style_text_color(l_t, is_active ? lv_color_hex(0xFF4D4D) : lv_color_hex(0xFFFFFF), 0);
                lv_obj_set_style_text_font(l_t, &lv_font_montserrat_14, 0);

                lv_obj_t* l_s = lv_label_create(btn);
                lv_label_set_text(l_s, subtitle);
                lv_obj_set_style_text_color(l_s, lv_color_hex(0x8A8A96), 0);
                lv_obj_set_style_text_font(l_s, &lv_font_montserrat_10, 0);

                lv_obj_add_event_cb(btn, [](lv_event_t* e){
                    uintptr_t m_val = reinterpret_cast<uintptr_t>(lv_event_get_user_data(e));
                    settings::ConnectionMode target_m = static_cast<settings::ConnectionMode>(m_val);
                    if (target_m == settings::current().connection_mode) return;
                    settings::setConnectionMode(target_m);
                    if (target_m == settings::ConnectionMode::DirectCan) {
                        abarth::obd::obd().forgetDevice();
                        abarth::can::can().start();
                    } else if (target_m == settings::ConnectionMode::BleObd) {
                        abarth::can::can().stop();
                        abarth::obd::obd().connectBLE();
                    } else if (target_m == settings::ConnectionMode::WifiObd) {
                        abarth::can::can().stop();
                        abarth::obd::obd().connectWiFi();
                    }
                    open_category(0, 0);
                }, LV_EVENT_CLICKED, reinterpret_cast<void*>(static_cast<uintptr_t>(m)));
            };

            make_mode_btn(LV_SYMBOL_BLUETOOTH, "Bluetooth LE", "Adattatore vLinker FD / ELM327", settings::ConnectionMode::BleObd);
            make_mode_btn(LV_SYMBOL_WIFI, "WiFi Dongle", "Adattatore OBD Socket TCP/IP", settings::ConnectionMode::WifiObd);
            make_mode_btn(LV_SYMBOL_DRIVE, "CAN-Bus Diretto", "WeAct ISOCANFD V1 (TWAI)", settings::ConnectionMode::DirectCan);

            // Card 1: CAN-Bus Diretto Duale (ESP32-P4 + 2x WeAct ISOCANFD V1)
            lv_obj_t* c_can = make_card_group(g_content_panel, "Dual CAN-Bus Diretto (ESP32-P4 + WeAct ISOCAN)", LV_SYMBOL_DRIVE);

            lv_obj_t* can_info = lv_label_create(c_can);
            lv_obj_set_style_text_font(can_info, &lv_font_montserrat_12, 0);
            lv_obj_set_style_text_color(can_info, lv_color_hex(0x00E5FF), 0);
            lv_label_set_text(can_info,
                "• Hardware: 2x Moduli WeAct Studio ISOCANFD V1 (CA-IS2062A isolamento 2.5 kV)\n"
                "• Supporta la lettura e scrittura contemporanea su entrambe le reti CAN vettura\n"
                "• I canali possono essere abilitati o disabilitati in modo totalmente indipendente.");

            // ==================== SEZIONE CAN 1 (C-CAN) ====================
            lv_obj_t* sec_can1 = lv_obj_create(c_can);
            lv_obj_remove_style_all(sec_can1);
            lv_obj_set_size(sec_can1, LV_PCT(100), LV_SIZE_CONTENT);
            lv_obj_set_style_bg_color(sec_can1, lv_color_hex(0x181824), 0);
            lv_obj_set_style_bg_opa(sec_can1, LV_OPA_COVER, 0);
            lv_obj_set_style_radius(sec_can1, 8, 0);
            lv_obj_set_style_pad_all(sec_can1, 10, 0);
            lv_obj_set_flex_flow(sec_can1, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_style_pad_row(sec_can1, 8, 0);

            // Row 1: Header + Switch CAN 1
            lv_obj_t* r_can1_hdr = lv_obj_create(sec_can1);
            lv_obj_set_width(r_can1_hdr, LV_PCT(100));
            lv_obj_set_style_bg_opa(r_can1_hdr, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(r_can1_hdr, 0, 0);
            lv_obj_set_style_pad_all(r_can1_hdr, 0, 0);
            lv_obj_set_flex_flow(r_can1_hdr, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(r_can1_hdr, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

            lv_obj_t* l_c1_t = lv_label_create(r_can1_hdr);
            lv_label_set_text(l_c1_t, "CAN 1: C-CAN Powertrain (Motore)");
            lv_obj_set_style_text_color(l_c1_t, lv_color_hex(0xFFFFFF), 0);
            lv_obj_set_style_text_font(l_c1_t, &lv_font_montserrat_14, 0);

            lv_obj_t* sw_can1 = lv_switch_create(r_can1_hdr);
            lv_obj_set_style_bg_color(sw_can1, lv_color_hex(0x30CC60), (lv_style_selector_t)((uint32_t)LV_PART_INDICATOR | (uint32_t)LV_STATE_CHECKED));
            if (s.can1_enabled) lv_obj_add_state(sw_can1, LV_STATE_CHECKED);
            lv_obj_add_event_cb(sw_can1, [](lv_event_t* e){
                lv_obj_t* target = lv_event_get_target(e);
                bool en = lv_obj_has_state(target, LV_STATE_CHECKED);
                settings::setCan1Enabled(en);
                if (settings::current().connection_mode == settings::ConnectionMode::DirectCan) {
                    if (en) abarth::can::can().startBus(0);
                    else    abarth::can::can().stopBus(0);
                }
            }, LV_EVENT_VALUE_CHANGED, nullptr);

            // Row 2: Info pin + riavvia CAN 1
            lv_obj_t* r_can1_ctrl = lv_obj_create(sec_can1);
            lv_obj_set_width(r_can1_ctrl, LV_PCT(100));
            lv_obj_set_style_bg_opa(r_can1_ctrl, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(r_can1_ctrl, 0, 0);
            lv_obj_set_style_pad_all(r_can1_ctrl, 0, 0);
            lv_obj_set_flex_flow(r_can1_ctrl, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(r_can1_ctrl, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

            lv_obj_t* l_c1_pin = lv_label_create(r_can1_ctrl);
            char c1_pbuf[48];
            snprintf(c1_pbuf, sizeof(c1_pbuf), "Pinout: TX GPIO %u | RX GPIO %u", s.can1_tx_pin, s.can1_rx_pin);
            lv_label_set_text(l_c1_pin, c1_pbuf);
            lv_obj_set_style_text_color(l_c1_pin, lv_color_hex(0x8A8A96), 0);
            lv_obj_set_style_text_font(l_c1_pin, &lv_font_montserrat_12, 0);

            lv_obj_t* btn_can1_rst = lv_btn_create(r_can1_ctrl);
            lv_obj_set_height(btn_can1_rst, 32);
            lv_obj_set_style_bg_color(btn_can1_rst, lv_color_hex(0x282838), 0);
            lv_obj_set_style_radius(btn_can1_rst, 6, 0);
            lv_obj_set_style_pad_hor(btn_can1_rst, 12, 0);
            lv_obj_t* lbl_c1rst = lv_label_create(btn_can1_rst);
            lv_label_set_text(lbl_c1rst, LV_SYMBOL_REFRESH " Riavvia C-CAN");
            lv_obj_set_style_text_color(lbl_c1rst, lv_color_hex(0xFFFFFF), 0);
            lv_obj_set_style_text_font(lbl_c1rst, &lv_font_montserrat_12, 0);
            lv_obj_center(lbl_c1rst);
            lv_obj_add_event_cb(btn_can1_rst, [](lv_event_t*){
                abarth::can::can().restartBus(0);
            }, LV_EVENT_CLICKED, nullptr);

            // Row 3: Titolo Velocita' C-CAN
            lv_obj_t* lbl_c1_baud_title = lv_label_create(sec_can1);
            lv_label_set_text(lbl_c1_baud_title, "Velocita' Bus C-CAN (Baud Rate):");
            lv_obj_set_style_text_color(lbl_c1_baud_title, lv_color_hex(0xDEDEDE), 0);
            lv_obj_set_style_text_font(lbl_c1_baud_title, &lv_font_montserrat_12, 0);

            // Row 4: Pulsanti di Selezione Velocita' (Segmented Buttons - Crash-Free)
            lv_obj_t* r_can1_baud = lv_obj_create(sec_can1);
            lv_obj_set_width(r_can1_baud, LV_PCT(100));
            lv_obj_set_style_bg_opa(r_can1_baud, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(r_can1_baud, 0, 0);
            lv_obj_set_style_pad_all(r_can1_baud, 0, 0);
            lv_obj_set_flex_flow(r_can1_baud, LV_FLEX_FLOW_ROW_WRAP);
            lv_obj_set_style_pad_gap(r_can1_baud, 8, 0);

            struct CanSpeedOpt {
                const char* label;
                uint32_t baud;
            };

            static const CanSpeedOpt c1_opts[] = {
                { "500 kbps (Abarth)", 500000 },
                { "250 kbps",          250000 },
                { "1000 kbps (1M)",   1000000 }
            };

            for (const auto& opt : c1_opts) {
                const bool is_sel = (s.can1_baud_rate == opt.baud);
                lv_obj_t* b = lv_btn_create(r_can1_baud);
                lv_obj_set_flex_grow(b, 1);
                lv_obj_set_height(b, 36);
                lv_obj_set_style_radius(b, 6, 0);
                lv_obj_set_style_border_width(b, is_sel ? 2 : 1, 0);
                lv_obj_set_style_border_color(b, is_sel ? lv_color_hex(0xCC3030) : lv_color_hex(0x383848), 0);
                lv_obj_set_style_bg_color(b, is_sel ? lv_color_hex(0x3A181C) : lv_color_hex(0x1C1C26), 0);
                lv_obj_set_style_pad_hor(b, 8, 0);
                lv_obj_set_style_pad_ver(b, 4, 0);

                lv_obj_t* l = lv_label_create(b);
                lv_label_set_text(l, opt.label);
                lv_obj_set_style_text_color(l, is_sel ? lv_color_hex(0xFF4D4D) : lv_color_hex(0x9E9EA8), 0);
                lv_obj_set_style_text_font(l, &lv_font_montserrat_12, 0);
                lv_obj_center(l);

                lv_obj_add_event_cb(b, [](lv_event_t* e){
                    uint32_t chosen_baud = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
                    const auto& cur = settings::current();
                    if (cur.can1_baud_rate == chosen_baud) return;
                    settings::setCan1Config(cur.can1_tx_pin, cur.can1_rx_pin, chosen_baud);
                    if (cur.connection_mode == settings::ConnectionMode::DirectCan && cur.can1_enabled) {
                        abarth::can::can().restartBus(0);
                    }
                    open_category(0, 0);
                }, LV_EVENT_CLICKED, reinterpret_cast<void*>(static_cast<uintptr_t>(opt.baud)));
            }

            // Live Diagnostic Box CAN 1
            g_lbl_can1_diag = lv_label_create(sec_can1);
            lv_obj_add_event_cb(g_lbl_can1_diag, [](lv_event_t*) {
                g_lbl_can1_diag = nullptr;
            }, LV_EVENT_DELETE, nullptr);
            lv_obj_set_style_text_font(g_lbl_can1_diag, &lv_font_montserrat_12, 0);
            lv_obj_set_style_text_color(g_lbl_can1_diag, lv_color_hex(0x30CC60), 0);
            lv_label_set_text(g_lbl_can1_diag, "CAN 1: In attesa di avvio...");

            // ==================== SEZIONE CAN 2 (B-CAN) ====================
            lv_obj_t* sec_can2 = lv_obj_create(c_can);
            lv_obj_remove_style_all(sec_can2);
            lv_obj_set_size(sec_can2, LV_PCT(100), LV_SIZE_CONTENT);
            lv_obj_set_style_bg_color(sec_can2, lv_color_hex(0x181824), 0);
            lv_obj_set_style_bg_opa(sec_can2, LV_OPA_COVER, 0);
            lv_obj_set_style_radius(sec_can2, 8, 0);
            lv_obj_set_style_pad_all(sec_can2, 10, 0);
            lv_obj_set_flex_flow(sec_can2, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_style_pad_row(sec_can2, 8, 0);

            // Row 1: Header + Switch CAN 2
            lv_obj_t* r_can2_hdr = lv_obj_create(sec_can2);
            lv_obj_set_width(r_can2_hdr, LV_PCT(100));
            lv_obj_set_style_bg_opa(r_can2_hdr, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(r_can2_hdr, 0, 0);
            lv_obj_set_style_pad_all(r_can2_hdr, 0, 0);
            lv_obj_set_flex_flow(r_can2_hdr, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(r_can2_hdr, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

            lv_obj_t* l_c2_t = lv_label_create(r_can2_hdr);
            lv_label_set_text(l_c2_t, "CAN 2: B-CAN Carrozzeria (Body)");
            lv_obj_set_style_text_color(l_c2_t, lv_color_hex(0xFFFFFF), 0);
            lv_obj_set_style_text_font(l_c2_t, &lv_font_montserrat_14, 0);

            lv_obj_t* sw_can2 = lv_switch_create(r_can2_hdr);
            lv_obj_set_style_bg_color(sw_can2, lv_color_hex(0x30CC60), (lv_style_selector_t)((uint32_t)LV_PART_INDICATOR | (uint32_t)LV_STATE_CHECKED));
            if (s.can2_enabled) lv_obj_add_state(sw_can2, LV_STATE_CHECKED);
            lv_obj_add_event_cb(sw_can2, [](lv_event_t* e){
                lv_obj_t* target = lv_event_get_target(e);
                bool en = lv_obj_has_state(target, LV_STATE_CHECKED);
                settings::setCan2Enabled(en);
                if (settings::current().connection_mode == settings::ConnectionMode::DirectCan) {
                    if (en) abarth::can::can().startBus(1);
                    else    abarth::can::can().stopBus(1);
                }
            }, LV_EVENT_VALUE_CHANGED, nullptr);

            // Row 2: Info pin + riavvia CAN 2
            lv_obj_t* r_can2_ctrl = lv_obj_create(sec_can2);
            lv_obj_set_width(r_can2_ctrl, LV_PCT(100));
            lv_obj_set_style_bg_opa(r_can2_ctrl, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(r_can2_ctrl, 0, 0);
            lv_obj_set_style_pad_all(r_can2_ctrl, 0, 0);
            lv_obj_set_flex_flow(r_can2_ctrl, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(r_can2_ctrl, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

            lv_obj_t* l_c2_pin = lv_label_create(r_can2_ctrl);
            char c2_pbuf[48];
            snprintf(c2_pbuf, sizeof(c2_pbuf), "Pinout: TX GPIO %u | RX GPIO %u", s.can2_tx_pin, s.can2_rx_pin);
            lv_label_set_text(l_c2_pin, c2_pbuf);
            lv_obj_set_style_text_color(l_c2_pin, lv_color_hex(0x8A8A96), 0);
            lv_obj_set_style_text_font(l_c2_pin, &lv_font_montserrat_12, 0);

            lv_obj_t* btn_can2_rst = lv_btn_create(r_can2_ctrl);
            lv_obj_set_height(btn_can2_rst, 32);
            lv_obj_set_style_bg_color(btn_can2_rst, lv_color_hex(0x282838), 0);
            lv_obj_set_style_radius(btn_can2_rst, 6, 0);
            lv_obj_set_style_pad_hor(btn_can2_rst, 12, 0);
            lv_obj_t* lbl_c2rst = lv_label_create(btn_can2_rst);
            lv_label_set_text(lbl_c2rst, LV_SYMBOL_REFRESH " Riavvia B-CAN");
            lv_obj_set_style_text_color(lbl_c2rst, lv_color_hex(0xFFFFFF), 0);
            lv_obj_set_style_text_font(lbl_c2rst, &lv_font_montserrat_12, 0);
            lv_obj_center(lbl_c2rst);
            lv_obj_add_event_cb(btn_can2_rst, [](lv_event_t*){
                abarth::can::can().restartBus(1);
            }, LV_EVENT_CLICKED, nullptr);

            // Row 3: Titolo Velocita' B-CAN
            lv_obj_t* lbl_c2_baud_title = lv_label_create(sec_can2);
            lv_label_set_text(lbl_c2_baud_title, "Velocita' Bus B-CAN (Baud Rate):");
            lv_obj_set_style_text_color(lbl_c2_baud_title, lv_color_hex(0xDEDEDE), 0);
            lv_obj_set_style_text_font(lbl_c2_baud_title, &lv_font_montserrat_12, 0);

            // Row 4: Pulsanti di Selezione Velocita' (Segmented Buttons - Crash-Free)
            lv_obj_t* r_can2_baud = lv_obj_create(sec_can2);
            lv_obj_set_width(r_can2_baud, LV_PCT(100));
            lv_obj_set_style_bg_opa(r_can2_baud, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(r_can2_baud, 0, 0);
            lv_obj_set_style_pad_all(r_can2_baud, 0, 0);
            lv_obj_set_flex_flow(r_can2_baud, LV_FLEX_FLOW_ROW_WRAP);
            lv_obj_set_style_pad_gap(r_can2_baud, 8, 0);

            static const CanSpeedOpt c2_opts[] = {
                { "50 kbps (Body)", 50000 },
                { "125 kbps",      125000 },
                { "250 kbps",      250000 },
                { "500 kbps",      500000 }
            };

            for (const auto& opt : c2_opts) {
                const bool is_sel = (s.can2_baud_rate == opt.baud);
                lv_obj_t* b = lv_btn_create(r_can2_baud);
                lv_obj_set_flex_grow(b, 1);
                lv_obj_set_height(b, 36);
                lv_obj_set_style_radius(b, 6, 0);
                lv_obj_set_style_border_width(b, is_sel ? 2 : 1, 0);
                lv_obj_set_style_border_color(b, is_sel ? lv_color_hex(0xCC3030) : lv_color_hex(0x383848), 0);
                lv_obj_set_style_bg_color(b, is_sel ? lv_color_hex(0x3A181C) : lv_color_hex(0x1C1C26), 0);
                lv_obj_set_style_pad_hor(b, 8, 0);
                lv_obj_set_style_pad_ver(b, 4, 0);

                lv_obj_t* l = lv_label_create(b);
                lv_label_set_text(l, opt.label);
                lv_obj_set_style_text_color(l, is_sel ? lv_color_hex(0xFF4D4D) : lv_color_hex(0x9E9EA8), 0);
                lv_obj_set_style_text_font(l, &lv_font_montserrat_12, 0);
                lv_obj_center(l);

                lv_obj_add_event_cb(b, [](lv_event_t* e){
                    uint32_t chosen_baud = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
                    const auto& cur = settings::current();
                    if (cur.can2_baud_rate == chosen_baud) return;
                    settings::setCan2Config(cur.can2_tx_pin, cur.can2_rx_pin, chosen_baud);
                    if (cur.connection_mode == settings::ConnectionMode::DirectCan && cur.can2_enabled) {
                        abarth::can::can().restartBus(1);
                    }
                    open_category(0, 0);
                }, LV_EVENT_CLICKED, reinterpret_cast<void*>(static_cast<uintptr_t>(opt.baud)));
            }

            // Live Diagnostic Box CAN 2
            g_lbl_can2_diag = lv_label_create(sec_can2);
            lv_obj_add_event_cb(g_lbl_can2_diag, [](lv_event_t*) {
                g_lbl_can2_diag = nullptr;
            }, LV_EVENT_DELETE, nullptr);
            lv_obj_set_style_text_font(g_lbl_can2_diag, &lv_font_montserrat_12, 0);
            lv_obj_set_style_text_color(g_lbl_can2_diag, lv_color_hex(0x30CC60), 0);
            lv_label_set_text(g_lbl_can2_diag, "CAN 2: In attesa di avvio...");

            // Switch Listen-Only Globale
            lv_obj_t* r_lo = lv_obj_create(c_can);
            lv_obj_set_width(r_lo, LV_PCT(100));
            lv_obj_set_style_bg_opa(r_lo, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(r_lo, 0, 0);
            lv_obj_set_style_pad_all(r_lo, 0, 0);
            lv_obj_set_flex_flow(r_lo, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(r_lo, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

            lv_obj_t* lbl_lo = lv_label_create(r_lo);
            lv_label_set_text(lbl_lo, "Sola Ricezione Passiva (Listen-Only)");
            lv_obj_set_style_text_color(lbl_lo, lv_color_hex(0xFFFFFF), 0);
            lv_obj_set_style_text_font(lbl_lo, &lv_font_montserrat_14, 0);

            lv_obj_t* sw_lo = lv_switch_create(r_lo);
            lv_obj_set_style_bg_color(sw_lo, lv_color_hex(0xCC3030), (lv_style_selector_t)((uint32_t)LV_PART_INDICATOR | (uint32_t)LV_STATE_CHECKED));
            if (s.can_listen_only) lv_obj_add_state(sw_lo, LV_STATE_CHECKED);
            lv_obj_add_event_cb(sw_lo, [](lv_event_t* e){
                lv_obj_t* target = lv_event_get_target(e);
                bool lo = lv_obj_has_state(target, LV_STATE_CHECKED);
                settings::setCanListenOnly(lo);
                if (settings::current().connection_mode == settings::ConnectionMode::DirectCan) {
                    abarth::can::can().restart();
                }
            }, LV_EVENT_VALUE_CHANGED, nullptr);

            // ==================== CARD SCHEMA DI COLLEGAMENTO ====================
            lv_obj_t* c_wiring = make_card_group(g_content_panel, "Schema Collegamento Hardware (Pin-to-Pin)", LV_SYMBOL_FILE);

            lv_obj_t* wiring_text = lv_label_create(c_wiring);
            lv_obj_set_style_text_font(wiring_text, &lv_font_montserrat_12, 0);
            lv_obj_set_style_text_color(wiring_text, lv_color_hex(0xE0E0EE), 0);
            lv_label_set_text(wiring_text,
                "#FF4444 [!] ALIMENTAZIONE MODULI WEACT ISOCANFD V1:#\n"
                "Ciascun modulo richiede ENTRAMBE le tensioni collegate:\n"
                "• #FFD700 5V (VDDP):# Alimenta il convertitore DC-DC isolato interno (lato CAN).\n"
                "  SENZA la 5V il lato bus isolato e' SPENTO e non riceve/trasmette nulla!\n"
                "• #00E5FF 3V3 (VDDL):# Alimenta i buffer logici TX/RX di ESP32-P4.\n"
                "• #AAAAAA GND:# Massa comune logica.\n"
                "• #FFCC00 JUMPER 120R:# Lasciare APERTO su entrambi i moduli (terminazione gia' presente sull'auto).\n\n"
                "#30CC60 --- MODULO 1: C-CAN Powertrain (Alta velocita' 500 kbps) ---#\n"
                "• Pin 5V Display       --->  Pin 5V Modulo 1 (VDDP)\n"
                "• Pin 3V3 Display      --->  Pin 3V3 Modulo 1 (VDDL)\n"
                "• Pin GND Display      --->  Pin GND Modulo 1\n"
                "• Pin GPIO 20 Display  --->  Pin TXD Modulo 1 (TWAI0 TX)\n"
                "• Pin GPIO 21 Display  --->  Pin RXD Modulo 1 (TWAI0 RX)\n"
                "• Pin CAN_H Modulo 1   --->  Presa OBD-II Auto Pin 6 (CAN-H Motore)\n"
                "• Pin CAN_L Modulo 1   --->  Presa OBD-II Auto Pin 14 (CAN-L Motore)\n\n"
                "#30CC60 --- MODULO 2: B-CAN Body (Bassa velocita' 50 kbps) ---#\n"
                "• Pin 5V Display       --->  Pin 5V Modulo 2 (VDDP)\n"
                "• Pin 3V3 Display      --->  Pin 3V3 Modulo 2 (VDDL)\n"
                "• Pin GND Display      --->  Pin GND Modulo 2\n"
                "• Pin GPIO 32 Display  --->  Pin TXD Modulo 2 (TWAI1 TX)\n"
                "• Pin GPIO 33 Display  --->  Pin RXD Modulo 2 (TWAI1 RX)\n"
                "• Pin CAN_H Modulo 2   --->  Presa OBD-II Auto Pin 1 (B-CAN-H Body)\n"
                "• Pin CAN_L Modulo 2   --->  Presa OBD-II Auto Pin 9 (B-CAN-L Body)\n\n"
                "#9A9AA6 --- PRESA DIAGNOSI OBD-II AUTO (Fiat 500 / Abarth 595) ---#\n"
                "• Pin 4 / Pin 5: Massa Telaio Auto (GND)\n"
                "• Pin 16: +12V Batteria Auto");
            lv_label_set_recolor(wiring_text, true);

            // Card 2: Riconnessione Automatica & Stato OBD (Dongle BLE / WiFi)
            lv_obj_t* c1 = make_card_group(g_content_panel, "Rete OBD-II Dongle", LV_SYMBOL_SETTINGS);
            
            lv_obj_t* r_auto = lv_obj_create(c1);
            lv_obj_set_width(r_auto, LV_PCT(100));
            lv_obj_set_style_bg_opa(r_auto, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(r_auto, 0, 0);
            lv_obj_set_style_pad_all(r_auto, 0, 0);
            lv_obj_set_flex_flow(r_auto, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(r_auto, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

            lv_obj_t* lbl_auto = lv_label_create(r_auto);
            lv_label_set_text(lbl_auto, "Riconnessione Automatica");
            lv_obj_set_style_text_color(lbl_auto, lv_color_hex(0xFFFFFF), 0);
            lv_obj_set_style_text_font(lbl_auto, &lv_font_montserrat_16, 0);

            lv_obj_t* sw_auto = lv_switch_create(r_auto);
            lv_obj_set_style_bg_color(sw_auto, lv_color_hex(0xCC3030), (lv_style_selector_t)((uint32_t)LV_PART_INDICATOR | (uint32_t)LV_STATE_CHECKED));
            if (s.auto_connect) lv_obj_add_state(sw_auto, LV_STATE_CHECKED);
            lv_obj_add_event_cb(sw_auto, auto_connect_sw_cb, LV_EVENT_VALUE_CHANGED, nullptr);

            g_lbl_obd_status = lv_label_create(c1);
            lv_label_set_text(g_lbl_obd_status, "Stato OBD: --");
            lv_obj_set_style_text_color(g_lbl_obd_status, lv_color_hex(0x00E5FF), 0);
            lv_obj_set_style_text_font(g_lbl_obd_status, &lv_font_montserrat_14, 0);

            // Card 2: Wi-Fi
            lv_obj_t* c_wifi = make_card_group(g_content_panel, "Wi-Fi (ESP32-C6)", LV_SYMBOL_WIFI);

            g_lbl_wifi_status = lv_label_create(c_wifi);
            lv_label_set_text(g_lbl_wifi_status, "WiFi: Disconnesso");
            lv_obj_set_style_text_color(g_lbl_wifi_status, lv_color_hex(0xAAAAAA), 0);
            lv_obj_set_style_text_font(g_lbl_wifi_status, &lv_font_montserrat_14, 0);

            lv_obj_t* r_w_btns = lv_obj_create(c_wifi);
            lv_obj_set_width(r_w_btns, LV_PCT(100));
            lv_obj_set_style_bg_opa(r_w_btns, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(r_w_btns, 0, 0);
            lv_obj_set_style_pad_all(r_w_btns, 0, 0);
            lv_obj_set_flex_flow(r_w_btns, LV_FLEX_FLOW_ROW_WRAP);
            lv_obj_set_style_pad_gap(r_w_btns, 12, 0);

            lv_obj_t* btn_wifi = lv_btn_create(r_w_btns);
            lv_obj_set_flex_grow(btn_wifi, 1);
            lv_obj_set_style_min_width(btn_wifi, 180, 0);
            lv_obj_set_height(btn_wifi, 44);
            lv_obj_set_style_bg_color(btn_wifi, lv_color_hex(0x282838), 0);
            lv_obj_set_style_radius(btn_wifi, 8, 0);
            lv_obj_t* lbl_w = lv_label_create(btn_wifi);
            lv_label_set_text(lbl_w, LV_SYMBOL_WIFI " Scansiona Wi-Fi");
            lv_obj_set_style_text_color(lbl_w, lv_color_hex(0xFFFFFF), 0);
            lv_obj_set_style_text_font(lbl_w, &lv_font_montserrat_14, 0);
            lv_obj_center(lbl_w);
            lv_obj_add_event_cb(btn_wifi, wifi_scan_cb, LV_EVENT_CLICKED, nullptr);

            lv_obj_t* btn_dis_w = lv_btn_create(r_w_btns);
            lv_obj_set_flex_grow(btn_dis_w, 1);
            lv_obj_set_style_min_width(btn_dis_w, 180, 0);
            lv_obj_set_height(btn_dis_w, 44);
            lv_obj_set_style_bg_color(btn_dis_w, lv_color_hex(0x551111), 0);
            lv_obj_set_style_radius(btn_dis_w, 8, 0);
            lv_obj_t* lbl_dw = lv_label_create(btn_dis_w);
            lv_label_set_text(lbl_dw, LV_SYMBOL_CLOSE " Disconnetti Wi-Fi");
            lv_obj_set_style_text_color(lbl_dw, lv_color_hex(0xFFFFFF), 0);
            lv_obj_set_style_text_font(lbl_dw, &lv_font_montserrat_14, 0);
            lv_obj_center(lbl_dw);
            lv_obj_add_event_cb(btn_dis_w, disconnect_wifi_cb, LV_EVENT_CLICKED, nullptr);

            // Card 3: Bluetooth LE
            lv_obj_t* c_ble = make_card_group(g_content_panel, "Bluetooth LE (vLinker/OBDLink)", LV_SYMBOL_BLUETOOTH);

            g_lbl_ble_status = lv_label_create(c_ble);
            lv_label_set_text(g_lbl_ble_status, "BLE: Disconnesso");
            lv_obj_set_style_text_color(g_lbl_ble_status, lv_color_hex(0xDEDEDE), 0);
            lv_obj_set_style_text_font(g_lbl_ble_status, &lv_font_montserrat_14, 0);

            lv_obj_t* r_b_btns = lv_obj_create(c_ble);
            lv_obj_set_width(r_b_btns, LV_PCT(100));
            lv_obj_set_style_bg_opa(r_b_btns, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(r_b_btns, 0, 0);
            lv_obj_set_style_pad_all(r_b_btns, 0, 0);
            lv_obj_set_flex_flow(r_b_btns, LV_FLEX_FLOW_ROW_WRAP);
            lv_obj_set_style_pad_gap(r_b_btns, 12, 0);

            lv_obj_t* btn_ble = lv_btn_create(r_b_btns);
            lv_obj_set_flex_grow(btn_ble, 1);
            lv_obj_set_style_min_width(btn_ble, 180, 0);
            lv_obj_set_height(btn_ble, 44);
            lv_obj_set_style_bg_color(btn_ble, lv_color_hex(0x282838), 0);
            lv_obj_set_style_radius(btn_ble, 8, 0);
            lv_obj_t* lbl_b = lv_label_create(btn_ble);
            lv_label_set_text(lbl_b, LV_SYMBOL_BLUETOOTH " Scansiona BLE");
            lv_obj_set_style_text_color(lbl_b, lv_color_hex(0xFFFFFF), 0);
            lv_obj_set_style_text_font(lbl_b, &lv_font_montserrat_14, 0);
            lv_obj_center(lbl_b);
            lv_obj_add_event_cb(btn_ble, ble_scan_cb, LV_EVENT_CLICKED, nullptr);

            lv_obj_t* btn_dis_b = lv_btn_create(r_b_btns);
            lv_obj_set_flex_grow(btn_dis_b, 1);
            lv_obj_set_style_min_width(btn_dis_b, 180, 0);
            lv_obj_set_height(btn_dis_b, 44);
            lv_obj_set_style_bg_color(btn_dis_b, lv_color_hex(0x551111), 0);
            lv_obj_set_style_radius(btn_dis_b, 8, 0);
            lv_obj_t* lbl_db = lv_label_create(btn_dis_b);
            lv_label_set_text(lbl_db, LV_SYMBOL_CLOSE " Disconnetti BLE");
            lv_obj_set_style_text_color(lbl_db, lv_color_hex(0xFFFFFF), 0);
            lv_obj_set_style_text_font(lbl_db, &lv_font_montserrat_14, 0);
            lv_obj_center(lbl_db);
            lv_obj_add_event_cb(btn_dis_b, disconnect_ble_cb, LV_EVENT_CLICKED, nullptr);
            break;
        }

        // === 1: TEMI GRAFICI ===
        case 1: {
            lv_obj_t* c = make_card_group(g_content_panel, "Stile Quadro Strumenti", LV_SYMBOL_IMAGE);
            make_sub_label(c, "Tema attivo nel cruscotto digitale:");

            lv_obj_t* info_box = lv_obj_create(c);
            lv_obj_remove_style_all(info_box);
            lv_obj_set_size(info_box, LV_PCT(100), LV_SIZE_CONTENT);
            lv_obj_set_style_bg_color(info_box, lv_color_hex(0x181824), 0);
            lv_obj_set_style_bg_opa(info_box, LV_OPA_COVER, 0);
            lv_obj_set_style_radius(info_box, 10, 0);
            lv_obj_set_style_border_width(info_box, 1, 0);
            lv_obj_set_style_border_color(info_box, lv_color_hex(0x28283C), 0);
            lv_obj_set_style_pad_all(info_box, 14, 0);
            lv_obj_set_flex_flow(info_box, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_style_pad_row(info_box, 6, 0);

            lv_obj_t* lbl_t = lv_label_create(info_box);
            lv_label_set_text(lbl_t, LV_SYMBOL_OK " Minimal Funzionale (Cockpit Essenziale)");
            lv_obj_set_style_text_color(lbl_t, lv_color_hex(0x30CC60), 0);
            lv_obj_set_style_text_font(lbl_t, &lv_font_montserrat_16, 0);

            lv_obj_t* lbl_d = lv_label_create(info_box);
            lv_label_set_text(lbl_d, "Interfaccia minimalista ad altissimo contrasto: tachimetro digitale, barra giri progressiva e strisce telemetriche a risposta istantanea.");
            lv_obj_set_style_text_color(lbl_d, lv_color_hex(0x9A9AA6), 0);
            lv_obj_set_style_text_font(lbl_d, &lv_font_montserrat_12, 0);
            break;
        }

        // === 2: SFONDI & TEXTURE ===
        case 2: {
            lv_obj_t* c = make_card_group(g_content_panel, "Texture di Sfondo Schermo", LV_SYMBOL_SHUFFLE);
            make_sub_label(c, "Scegli lo sfondo grafico visualizzato sul display:");

            lv_obj_t* dd = lv_dropdown_create(c);
            lv_dropdown_set_options(dd,
                "Sfondo Originale (Default)\n"
                "Real Carbon Fiber Weave (Fibra di Carbonio 3D)\n"
                "Bandiera a Scacchi Racing\n"
                "Tricolore Italiano Sfumato\n"
                "Scorpione Abarth Watermark\n"
                "Asfalto Track Texture");

            uint16_t bg_sel = static_cast<uint16_t>(s.bg_texture);
            if (bg_sel > 5) bg_sel = 0;
            lv_dropdown_set_selected(dd, bg_sel);
            style_dropdown(dd, lv_color_hex(0xFF9900));

            lv_obj_add_event_cb(dd, [](lv_event_t* e){
                uint16_t sel = lv_dropdown_get_selected(lv_event_get_target(e));
                settings::BgTexture chosen = static_cast<settings::BgTexture>(sel);
                if (chosen != settings::current().bg_texture) {
                    settings::setBgTexture(chosen);
                    ESP_LOGI(TAG, "Cambiata Texture -> riavvio...");
                    vTaskDelay(pdMS_TO_TICKS(150));
                    esp_restart();
                }
            }, LV_EVENT_VALUE_CHANGED, nullptr);
            break;
        }

        // === 3: DISPLAY & SCHERMO ===
        case 3: {
            // Card 1: Luminosita' Backlight
            lv_obj_t* c1 = make_card_group(g_content_panel, "Retroilluminazione Display", LV_SYMBOL_EYE_OPEN);

            lv_obj_t* r_bright = lv_obj_create(c1);
            lv_obj_set_width(r_bright, LV_PCT(100));
            lv_obj_set_height(r_bright, LV_SIZE_CONTENT);
            lv_obj_set_style_bg_opa(r_bright, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(r_bright, 0, 0);
            lv_obj_set_style_pad_all(r_bright, 0, 0);
            lv_obj_set_flex_flow(r_bright, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(r_bright, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

            lv_obj_t* slider = lv_slider_create(r_bright);
            lv_obj_set_flex_grow(slider, 1);
            lv_slider_set_range(slider, 10, 100);
            lv_slider_set_value(slider, s.brightness, LV_ANIM_OFF);
            lv_obj_set_style_bg_color(slider, lv_color_hex(0x2A2A3A), LV_PART_MAIN);
            lv_obj_set_style_bg_color(slider, lv_color_hex(0xCC3030), LV_PART_INDICATOR);
            lv_obj_add_event_cb(slider, brightness_slider_cb, LV_EVENT_VALUE_CHANGED, nullptr);

            g_lbl_brightness_value = lv_label_create(r_bright);
            lv_obj_set_style_text_color(g_lbl_brightness_value, lv_color_hex(0xFFFFFF), 0);
            lv_obj_set_style_text_font(g_lbl_brightness_value, &lv_font_montserrat_18, 0);
            lv_obj_set_style_pad_left(g_lbl_brightness_value, 16, 0);
            char buf[8];
            std::snprintf(buf, sizeof(buf), "%u%%", static_cast<unsigned>(s.brightness));
            lv_label_set_text(g_lbl_brightness_value, buf);

            // Card 2: Orientamento Schermo
            lv_obj_t* c2 = make_card_group(g_content_panel, "Orientamento Schermo (Reboot)", LV_SYMBOL_REFRESH);
            make_sub_label(c2, "Configura la rotazione software del pannello LCD:");

            lv_obj_t* dd_o = lv_dropdown_create(c2);
            lv_dropdown_set_options(dd_o,
                "Orizzontale Landscape (1024x600 - Default)\n"
                "Verticale Portrait (600x1024)\n"
                "Verticale Capovolto (600x1024)");
            uint16_t o_sel = static_cast<uint16_t>(s.screen_orientation);
            if (o_sel > 2) o_sel = 0;
            lv_dropdown_set_selected(dd_o, o_sel);
            style_dropdown(dd_o, lv_color_hex(0xCC3030));

            lv_obj_add_event_cb(dd_o, [](lv_event_t* e){
                uint16_t sel = lv_dropdown_get_selected(lv_event_get_target(e));
                settings::ScreenOrientation chosen = static_cast<settings::ScreenOrientation>(sel);
                if (chosen != settings::current().screen_orientation) {
                    settings::setScreenOrientation(chosen);
                    ESP_LOGI(TAG, "Cambiato orientamento -> riavvio...");
                    vTaskDelay(pdMS_TO_TICKS(150));
                    esp_restart();
                }
            }, LV_EVENT_VALUE_CHANGED, nullptr);
            break;
        }

        // === 4: UNITA' DI MISURA ===
        case 4: {
            lv_obj_t* c = make_card_group(g_content_panel, "Unita' di Misura Telemetria", LV_SYMBOL_LIST);

            // Temperatura
            make_sub_label(c, "Temperatura (Liquido, Olio, IAT, Esterna, EGT)");

            lv_obj_t* dd_t = lv_dropdown_create(c);
            lv_dropdown_set_options(dd_t, "Celsius (\u00b0C)\nFahrenheit (\u00b0F)");
            lv_dropdown_set_selected(dd_t, static_cast<uint16_t>(s.temp_unit));
            style_dropdown(dd_t, lv_color_hex(0x00CC66));
            lv_obj_add_event_cb(dd_t, [](lv_event_t* e){
                uint16_t sel = lv_dropdown_get_selected(lv_event_get_target(e));
                settings::setTempUnit(static_cast<settings::TempUnit>(sel));
            }, LV_EVENT_VALUE_CHANGED, nullptr);

            // Velocita'
            make_sub_label(c, "Velocita' Veicolo");

            lv_obj_t* dd_s = lv_dropdown_create(c);
            lv_dropdown_set_options(dd_s, "Kilometri orari (km/h)\nMiglia orarie (mph)");
            lv_dropdown_set_selected(dd_s, static_cast<uint16_t>(s.speed_unit));
            style_dropdown(dd_s, lv_color_hex(0x00CC66));
            lv_obj_add_event_cb(dd_s, [](lv_event_t* e){
                uint16_t sel = lv_dropdown_get_selected(lv_event_get_target(e));
                settings::setSpeedUnit(static_cast<settings::SpeedUnit>(sel));
            }, LV_EVENT_VALUE_CHANGED, nullptr);

            // Pressione
            make_sub_label(c, "Pressione (MAP, Boost Turbo, Olio)");

            lv_obj_t* dd_p = lv_dropdown_create(c);
            lv_dropdown_set_options(dd_p, "kPa (kilopascal)\nbar (1 bar = 100 kPa)\nkPa + bar (entrambi)");
            uint16_t p_sel = static_cast<uint16_t>(s.pressure_unit);
            if (p_sel > 2) p_sel = 1;
            lv_dropdown_set_selected(dd_p, p_sel);
            style_dropdown(dd_p, lv_color_hex(0x00CC66));
            lv_obj_add_event_cb(dd_p, [](lv_event_t* e){
                uint16_t sel = lv_dropdown_get_selected(lv_event_get_target(e));
                settings::setPressureUnit(static_cast<settings::PressureUnit>(sel));
            }, LV_EVENT_VALUE_CHANGED, nullptr);
            break;
        }

        // === 5: TEST & SIMULAZIONE ===
        case 5: {
            // Card 1: Modalita' Debug Diagnostica
            lv_obj_t* c_dbg = make_card_group(g_content_panel, "Modalita' Debug Diagnostica", LV_SYMBOL_SETTINGS);
            make_sub_label(c_dbg, "Abilita l'accesso ai test avanzati, la simulazione telemetrica ed i dati tecnici nello splashscreen:");

            lv_obj_t* r_dbg = lv_obj_create(c_dbg);
            lv_obj_set_width(r_dbg, LV_PCT(100));
            lv_obj_set_style_bg_opa(r_dbg, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(r_dbg, 0, 0);
            lv_obj_set_style_pad_all(r_dbg, 0, 0);
            lv_obj_set_flex_flow(r_dbg, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(r_dbg, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

            lv_obj_t* lbl_dbg = lv_label_create(r_dbg);
            lv_label_set_text(lbl_dbg, "Attiva Modalita' Debug");
            lv_obj_set_style_text_color(lbl_dbg, lv_color_hex(0xFFFFFF), 0);
            lv_obj_set_style_text_font(lbl_dbg, &lv_font_montserrat_16, 0);

            lv_obj_t* sw_debug = lv_switch_create(r_dbg);
            lv_obj_set_style_bg_color(sw_debug, lv_color_hex(0x00CC66), (lv_style_selector_t)((uint32_t)LV_PART_INDICATOR | (uint32_t)LV_STATE_CHECKED));
            if (s.debug_mode) lv_obj_add_state(sw_debug, LV_STATE_CHECKED);
            lv_obj_add_event_cb(sw_debug, [](lv_event_t* e){
                bool enabled = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
                settings::setDebugMode(enabled);
                ESP_LOGI(TAG, "Debug Mode -> %s", enabled ? "ATTIVA" : "DISATTIVA");
                render_category(5);
            }, LV_EVENT_VALUE_CHANGED, nullptr);

            // Card 2: Modalita' Simulazione (Demo Mode)
            lv_obj_t* c1 = make_card_group(g_content_panel, "Simulazione Telemetria (Demo Mode)", LV_SYMBOL_REFRESH);
            make_sub_label(c1, s.debug_mode ? "Genera dati telemetrici dinamici per provare la grafica ed i manometri:"
                                           : "ATTENZIONE: Richiede 'Modalita' Debug Diagnostica' ATTIVA per essere sbloccata.");

            lv_obj_t* r_d = lv_obj_create(c1);
            lv_obj_set_width(r_d, LV_PCT(100));
            lv_obj_set_style_bg_opa(r_d, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(r_d, 0, 0);
            lv_obj_set_style_pad_all(r_d, 0, 0);
            lv_obj_set_flex_flow(r_d, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(r_d, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

            lv_obj_t* lbl_d = lv_label_create(r_d);
            lv_label_set_text(lbl_d, "Attiva Simulazione");
            lv_obj_set_style_text_color(lbl_d, s.debug_mode ? lv_color_hex(0xFFFFFF) : lv_color_hex(0x666677), 0);
            lv_obj_set_style_text_font(lbl_d, &lv_font_montserrat_16, 0);

            lv_obj_t* sw_demo = lv_switch_create(r_d);
            lv_obj_set_style_bg_color(sw_demo, lv_color_hex(0xFFD700), (lv_style_selector_t)((uint32_t)LV_PART_INDICATOR | (uint32_t)LV_STATE_CHECKED));
            if (s.demo_mode && s.debug_mode) {
                lv_obj_add_state(sw_demo, LV_STATE_CHECKED);
            }
            if (!s.debug_mode) {
                lv_obj_add_state(sw_demo, LV_STATE_DISABLED);
            } else {
                lv_obj_add_event_cb(sw_demo, [](lv_event_t* e){
                    bool enabled = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
                    settings::setDemoMode(enabled);
                    ESP_LOGI(TAG, "Simulazione -> %s", enabled ? "ATTIVA" : "DISATTIVA");
                }, LV_EVENT_VALUE_CHANGED, nullptr);
            }

            // Card 3: Diagnostica Touch Screen
            lv_obj_t* c2 = make_card_group(g_content_panel, "Diagnostica & Test Touch", LV_SYMBOL_EYE_OPEN);
            make_sub_label(c2, "Verifica la precisione e la calibrazione del pannello capacitivo GT911:");

            lv_obj_t* btn_touch = lv_btn_create(c2);
            lv_obj_set_width(btn_touch, LV_PCT(100));
            lv_obj_set_height(btn_touch, 46);
            lv_obj_set_style_bg_color(btn_touch, lv_color_hex(0x282838), 0);
            lv_obj_set_style_radius(btn_touch, 10, 0);
            lv_obj_t* lbl_bt = lv_label_create(btn_touch);
            lv_label_set_text(lbl_bt, LV_SYMBOL_EYE_OPEN " Avvia Test Touch Screen Interattivo");
            lv_obj_set_style_text_color(lbl_bt, lv_color_hex(0xFFFFFF), 0);
            lv_obj_set_style_text_font(lbl_bt, &lv_font_montserrat_14, 0);
            lv_obj_center(lbl_bt);
            lv_obj_add_event_cb(btn_touch, open_touch_test_modal, LV_EVENT_CLICKED, nullptr);
            break;
        }

        // === 6: VISIBILITA' MANOMETRI & INDICATORI ===
        case 6: {
            using abarth::data::Metric;

            // Barra Sottomenu Superiore (Motore, Temp, Press, Carb, Elettr)
            lv_obj_t* sub_bar = lv_obj_create(g_content_panel);
            lv_obj_set_width(sub_bar, LV_PCT(100));
            lv_obj_set_height(sub_bar, 48);
            lv_obj_set_style_bg_color(sub_bar, lv_color_hex(0x12121D), 0);
            lv_obj_set_style_bg_opa(sub_bar, LV_OPA_COVER, 0);
            lv_obj_set_style_border_color(sub_bar, lv_color_hex(0x28283A), 0);
            lv_obj_set_style_border_width(sub_bar, 1, 0);
            lv_obj_set_style_radius(sub_bar, 10, 0);
            lv_obj_set_style_pad_all(sub_bar, 4, 0);
            lv_obj_set_flex_flow(sub_bar, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(sub_bar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

            static const char* sub_labels[6] = {
                LV_SYMBOL_SHUFFLE " Custom",
                LV_SYMBOL_POWER " Motore",
                LV_SYMBOL_REFRESH " Temp.",
                LV_SYMBOL_DRIVE " Press.",
                LV_SYMBOL_BATTERY_FULL " Carb.",
                LV_SYMBOL_CHARGE " Elettr."
            };

            for (uint8_t i = 0; i < 6; ++i) {
                lv_obj_t* sbtn = lv_btn_create(sub_bar);
                lv_obj_set_flex_grow(sbtn, 1);
                lv_obj_set_height(sbtn, 38);
                lv_obj_set_style_radius(sbtn, 8, 0);
                if (i == g_subcat_metric) {
                    lv_obj_set_style_bg_color(sbtn, lv_color_hex(0xCC3030), 0);
                    lv_obj_set_style_border_width(sbtn, 0, 0);
                } else {
                    lv_obj_set_style_bg_color(sbtn, lv_color_hex(0x1C1C2A), 0);
                    lv_obj_set_style_border_color(sbtn, lv_color_hex(0x2A2A3E), 0);
                    lv_obj_set_style_border_width(sbtn, 1, 0);
                }
                lv_obj_t* slbl = lv_label_create(sbtn);
                lv_label_set_text(slbl, sub_labels[i]);
                lv_obj_set_style_text_color(slbl, lv_color_hex(0xFFFFFF), 0);
                lv_obj_set_style_text_font(slbl, &lv_font_montserrat_12, 0);
                lv_obj_center(slbl);

                lv_obj_add_event_cb(sbtn, [](lv_event_t* e){
                    uintptr_t sub_idx = (uintptr_t)lv_event_get_user_data(e);
                    g_subcat_metric = (uint8_t)sub_idx;
                    render_category(6);
                }, LV_EVENT_CLICKED, (void*)(uintptr_t)i);
            }

            // Sottomenu 0: Pannello Personalizzato (Custom Tab)
            if (g_subcat_metric == 0) {
                lv_obj_t* c0 = make_card_group(g_content_panel, "Pannello Personalizzato (Custom)", LV_SYMBOL_SHUFFLE);
                make_sub_label(c0, "Attiva o disattiva la scheda personalizzata e seleziona cosa visualizzare:");

                // Master Toggle per la Scheda Custom sulla barra principale
                lv_obj_t* row_m = lv_obj_create(c0);
                lv_obj_remove_style_all(row_m);
                lv_obj_set_width(row_m, LV_PCT(100));
                lv_obj_set_height(row_m, LV_SIZE_CONTENT);
                lv_obj_set_style_pad_ver(row_m, 8, 0);
                lv_obj_set_style_pad_hor(row_m, 4, 0);
                lv_obj_set_style_border_side(row_m, LV_BORDER_SIDE_BOTTOM, 0);
                lv_obj_set_style_border_color(row_m, lv_color_hex(0x222234), 0);
                lv_obj_set_style_border_width(row_m, 1, 0);
                lv_obj_set_flex_flow(row_m, LV_FLEX_FLOW_ROW);
                lv_obj_set_flex_align(row_m, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

                lv_obj_t* lbl_m = lv_label_create(row_m);
                lv_label_set_text(lbl_m, "Abilita Scheda sulla Dashboard");
                lv_obj_set_style_text_color(lbl_m, lv_color_hex(0xFFFFFF), 0);
                lv_obj_set_style_text_font(lbl_m, &lv_font_montserrat_14, 0);
                lv_obj_set_flex_grow(lbl_m, 1);

                lv_obj_t* sw_m = lv_switch_create(row_m);
                lv_obj_set_style_bg_color(sw_m, lv_color_hex(0x00CC66), (lv_style_selector_t)((uint32_t)LV_PART_INDICATOR | (uint32_t)LV_STATE_CHECKED));
                if (abarth::settings::isCustomTabEnabled()) {
                    lv_obj_add_state(sw_m, LV_STATE_CHECKED);
                }
                lv_obj_add_event_cb(sw_m, [](lv_event_t* e){
                    bool en = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
                    abarth::settings::setCustomTabEnabled(en);
                    abarth::ui::apply_metric_visibility();
                }, LV_EVENT_VALUE_CHANGED, nullptr);

                lv_obj_t* c_mot = make_card_group(g_content_panel, "Parametri Motore Custom", LV_SYMBOL_POWER);
                add_custom_metric_toggle(c_mot, Metric::Rpm, "Giri Motore (RPM)", "Visualizza contagiri con barra progressiva");
                add_custom_metric_toggle(c_mot, Metric::Speed, "Velocita' Veicolo", "Tachimetro digitale km/h o mph");
                add_custom_metric_toggle(c_mot, Metric::EngineLoad, "Carico Motore (%)", "Percentuale di sforzo del motore");
                add_custom_metric_toggle(c_mot, Metric::Throttle, "Posizione Farfalla (%)", "Apertura corpo farfallato");
                add_custom_metric_toggle(c_mot, Metric::Afr, "Rapporto Stechiometrico (AFR)", "Miscela aria/benzina sonda Lambda");
                add_custom_metric_toggle(c_mot, Metric::DistanceSinceClear, "Km da Reset DTC", "Chilometri percorsi da azzeramento");

                lv_obj_t* c_tmp = make_card_group(g_content_panel, "Temperature Custom", LV_SYMBOL_REFRESH);
                add_custom_metric_toggle(c_tmp, Metric::OilTempC, "Temp. Olio Motore", "Temperatura circuito olio lubrificante");
                add_custom_metric_toggle(c_tmp, Metric::CoolantTempC, "Temp. Liquido Raffreddamento", "Temperatura acqua radiatore");
                add_custom_metric_toggle(c_tmp, Metric::IntakeAirTempC, "Temp. Aria Aspirata (IAT)", "Sensore intercooler / aspirazione");
                add_custom_metric_toggle(c_tmp, Metric::EgtC, "Temp. Gas di Scarico (EGT)", "Temperatura a monte turbina");
                add_custom_metric_toggle(c_tmp, Metric::AmbientTempC, "Temp. Esterna", "Sensore temperatura ambiente");

                lv_obj_t* c_prs = make_card_group(g_content_panel, "Pressioni Custom", LV_SYMBOL_DRIVE);
                add_custom_metric_toggle(c_prs, Metric::BoostRelBar, "Pressione Turbo (Boost)", "Manometro turbo relativo");
                add_custom_metric_toggle(c_prs, Metric::BoostKpa, "Pressione Collettore (MAP)", "Pressione assoluta MAP");
                add_custom_metric_toggle(c_prs, Metric::OilPressureBar, "Pressione Olio Motore", "Sensore pressione circuito lubrificazione");
                add_custom_metric_toggle(c_prs, Metric::FuelPressureKpa, "Pressione Rail Carburante", "Sensore alta pressione benzina");

                lv_obj_t* c_ele = make_card_group(g_content_panel, "Carburante & Impianto Elettrico Custom", LV_SYMBOL_CHARGE);
                add_custom_metric_toggle(c_ele, Metric::BatteryVolts, "Tensione Batteria (Volts)", "Voltmetro alternatore e batteria");
                add_custom_metric_toggle(c_ele, Metric::FuelLevelPct, "Livello Serbatoio (%)", "Percentuale carburante nel serbatoio");
                add_custom_metric_toggle(c_ele, Metric::FuelConsumptionL100, "Consumo Istantaneo (L/100km)", "Consumo carburante calcolato");
                add_custom_metric_toggle(c_ele, Metric::FuelRateLh, "Portata Carburante (L/h)", "Flusso iniettori in litri all'ora");
                add_custom_metric_toggle(c_ele, Metric::EthanolPct, "Percentuale Etanolo (%)", "Sensore FlexFuel");
            }
            // Sottomenu 1: Motore & Prestazioni
            else if (g_subcat_metric == 1) {
                lv_obj_t* c1 = make_card_group(g_content_panel, "Motore & Prestazioni (Pannello Motore)", LV_SYMBOL_POWER);
                make_sub_label(c1, "Attiva o disattiva gli indicatori della sezione Motore (lo spazio viene riadattato dinamicamente):");
                add_metric_toggle(c1, Metric::Rpm, "Giri Motore (RPM)", "Visualizza contagiri principale");
                add_metric_toggle(c1, Metric::Speed, "Velocita' Veicolo", "Visualizza tachimetro km/h o mph");
                add_metric_toggle(c1, Metric::EngineLoad, "Carico Motore (%)", "Percentuale sforzo motore ECU");
                add_metric_toggle(c1, Metric::Throttle, "Posizione Acceleratore (%)", "Apertura farfalla pedale");
                add_metric_toggle(c1, Metric::Afr, "Rapporto Stechiometrico (AFR)", "Misura sonda Lambda O2");
                add_metric_toggle(c1, Metric::DistanceSinceClear, "Km da Reset DTC", "Chilometri da azzeramento errori");
            }
            // Sottomenu 2: Temperature
            else if (g_subcat_metric == 2) {
                lv_obj_t* c2 = make_card_group(g_content_panel, "Temperature Telemetria", LV_SYMBOL_REFRESH);
                make_sub_label(c2, "Attiva o disattiva gli indicatori di Temperatura:");
                add_metric_toggle(c2, Metric::CoolantTempC, "Temp. Liquido Refrigerante", "Temperatura acqua radiatore");
                add_metric_toggle(c2, Metric::IntakeAirTempC, "Temp. Aria Aspirata (IAT)", "Temperatura collettore aspirazione");
                add_metric_toggle(c2, Metric::OilTempC, "Temp. Olio Motore", "Temperatura circuito olio lubrificante");
                add_metric_toggle(c2, Metric::AmbientTempC, "Temp. Esterna", "Sensore temperatura ambiente");
                add_metric_toggle(c2, Metric::EgtC, "Temp. Gas di Scarico (EGT)", "Sensore EGT turbina/collettore");
            }
            // Sottomenu 3: Pressioni
            else if (g_subcat_metric == 3) {
                lv_obj_t* c3 = make_card_group(g_content_panel, "Pressioni Telemetria", LV_SYMBOL_DRIVE);
                make_sub_label(c3, "Attiva o disattiva i manometri di Pressione:");
                add_metric_toggle(c3, Metric::BoostRelBar, "Pressione Turbo (Boost)", "Manometro pressione turbo relativa");
                add_metric_toggle(c3, Metric::BoostKpa, "Pressione Collettore (MAP)", "Sensore MAP pressione assoluta");
                add_metric_toggle(c3, Metric::OilPressureBar, "Pressione Olio Motore", "Sensore pressione circuito olio");
            }
            // Sottomenu 4: Carburante & Consumi
            else if (g_subcat_metric == 4) {
                lv_obj_t* c4 = make_card_group(g_content_panel, "Carburante & Consumi", LV_SYMBOL_BATTERY_FULL);
                make_sub_label(c4, "Attiva o disattiva i dati Carburante:");
                add_metric_toggle(c4, Metric::FuelLevelPct, "Livello Serbatoio (%)", "Percentuale carburante rimasto");
                add_metric_toggle(c4, Metric::FuelConsumptionL100, "Consumo Istantaneo (L/100km)", "Calcolo consumo in tempo reale");
                add_metric_toggle(c4, Metric::FuelRateLh, "Portata Carburante (L/h)", "Flusso iniettori litri/ora");
                add_metric_toggle(c4, Metric::FuelPressureKpa, "Pressione Rail Carburante", "Sensore pressione pompa benzina");
                add_metric_toggle(c4, Metric::FuelTempC, "Temp. Carburante", "Temperatura benzina nel condotto");
                add_metric_toggle(c4, Metric::FuelInjectionTiming, "Anticipo Iniezione", "Angolo fasatura iniezione");
                add_metric_toggle(c4, Metric::EthanolPct, "Percentuale Etanolo (%)", "Sensore FlexFuel E85/E10");
            }
            // Sottomenu 5: Impianto Elettrico
            else if (g_subcat_metric == 5) {
                lv_obj_t* c5 = make_card_group(g_content_panel, "Impianto Elettrico", LV_SYMBOL_CHARGE);
                make_sub_label(c5, "Attiva o disattiva le misurazioni elettriche:");
                add_metric_toggle(c5, Metric::BatteryVolts, "Tensione Batteria (Volts)", "Voltmetro alternatore e batteria");
            }
            break;
        }

        // === 7: DIAGNOSTICA CODICI ERRORE (DTC) ===
        case 7: {
            // Card 1: Scansione Errori
            lv_obj_t* c1 = make_card_group(g_content_panel, "Lettura Errori Centralina (Mode 03/07)", LV_SYMBOL_WARNING);
            make_sub_label(c1, "Scansiona la memoria guasti dell'ECU Motore per verificare la presenza di codici errore (DTC):");

            lv_obj_t* btn_scan = lv_btn_create(c1);
            lv_obj_set_width(btn_scan, LV_PCT(100));
            lv_obj_set_height(btn_scan, 46);
            lv_obj_set_style_bg_color(btn_scan, lv_color_hex(0x0088CC), 0);
            lv_obj_set_style_radius(btn_scan, 10, 0);

            lv_obj_t* lbl_scan = lv_label_create(btn_scan);
            lv_label_set_text(lbl_scan, LV_SYMBOL_REFRESH " Avvia Scansione Errori ECU");
            lv_obj_set_style_text_color(lbl_scan, lv_color_hex(0xFFFFFF), 0);
            lv_obj_set_style_text_font(lbl_scan, &lv_font_montserrat_14, 0);
            lv_obj_center(lbl_scan);

            // Contenitore Risultati Scansione
            lv_obj_t* res_cont = lv_obj_create(c1);
            lv_obj_remove_style_all(res_cont);
            lv_obj_set_width(res_cont, LV_PCT(100));
            lv_obj_set_height(res_cont, LV_SIZE_CONTENT);
            lv_obj_set_flex_flow(res_cont, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_style_pad_top(res_cont, 8, 0);

            lv_obj_t* status_lbl = lv_label_create(res_cont);
            lv_label_set_text(status_lbl, "Premere 'Avvia Scansione Errori ECU' per verificare la memoria DTC.");
            lv_obj_set_style_text_color(status_lbl, lv_color_hex(0x9A9AA6), 0);
            lv_obj_set_style_text_font(status_lbl, &lv_font_montserrat_14, 0);

            lv_obj_add_event_cb(btn_scan, [](lv_event_t* e){
                lv_obj_t* res_box = (lv_obj_t*)lv_event_get_user_data(e);
                lv_obj_clean(res_box);

                std::vector<obd::DtcItem> codes;
                std::string status_msg;
                bool ok = obd::obd().readDtcCodes(codes, status_msg);

                lv_obj_t* st_lbl = lv_label_create(res_box);
                lv_label_set_text(st_lbl, status_msg.c_str());
                lv_obj_set_style_text_font(st_lbl, &lv_font_montserrat_14, 0);
                lv_obj_set_style_text_color(st_lbl, ok && codes.empty() ? lv_color_hex(0x00CC66) : lv_color_hex(0xFFCC00), 0);

                for (const auto& dtc : codes) {
                    lv_obj_t* item = lv_obj_create(res_box);
                    lv_obj_remove_style_all(item);
                    lv_obj_set_width(item, LV_PCT(100));
                    lv_obj_set_height(item, LV_SIZE_CONTENT);
                    lv_obj_set_style_bg_color(item, lv_color_hex(0x221111), 0);
                    lv_obj_set_style_bg_opa(item, LV_OPA_COVER, 0);
                    lv_obj_set_style_radius(item, 8, 0);
                    lv_obj_set_style_pad_all(item, 10, 0);
                    lv_obj_set_style_pad_top(item, 6, 0);
                    lv_obj_set_flex_flow(item, LV_FLEX_FLOW_COLUMN);

                    lv_obj_t* code_lbl = lv_label_create(item);
                    char c_buf[32];
                    std::snprintf(c_buf, sizeof(c_buf), "Codice: %s", dtc.code.c_str());
                    lv_label_set_text(code_lbl, c_buf);
                    lv_obj_set_style_text_color(code_lbl, lv_color_hex(0xFF3333), 0);
                    lv_obj_set_style_text_font(code_lbl, &lv_font_montserrat_16, 0);

                    lv_obj_t* desc_lbl = lv_label_create(item);
                    lv_label_set_text(desc_lbl, dtc.description.c_str());
                    lv_obj_set_style_text_color(desc_lbl, lv_color_hex(0xDEDEDE), 0);
                    lv_obj_set_style_text_font(desc_lbl, &lv_font_montserrat_14, 0);
                }
            }, LV_EVENT_CLICKED, res_cont);

            // Card 2: Cancellazione Errori (Reset MIL)
            lv_obj_t* c2 = make_card_group(g_content_panel, "Cancellazione Errori & Reset MIL", LV_SYMBOL_TRASH);
            make_sub_label(c2, "Azzera i codici guasto salvati e spegne la spia Check Engine (MIL). Eseguire a quadro acceso e motore spento:");

            lv_obj_t* btn_clear = lv_btn_create(c2);
            lv_obj_set_width(btn_clear, LV_PCT(100));
            lv_obj_set_height(btn_clear, 46);
            lv_obj_set_style_bg_color(btn_clear, lv_color_hex(0xCC0000), 0);
            lv_obj_set_style_radius(btn_clear, 10, 0);

            lv_obj_t* lbl_clear = lv_label_create(btn_clear);
            lv_label_set_text(lbl_clear, LV_SYMBOL_TRASH " Cancella Errori ECU & Reset Spia Motore");
            lv_obj_set_style_text_color(lbl_clear, lv_color_hex(0xFFFFFF), 0);
            lv_obj_set_style_text_font(lbl_clear, &lv_font_montserrat_14, 0);
            lv_obj_center(lbl_clear);

            lv_obj_add_event_cb(btn_clear, [](lv_event_t* e){
                // Controllo preventivo: motore spento e auto ferma (dati freschi)
                std::string why;
                if (!obd::obd().dtcClearAllowed(why)) {
                    create_modal("Cancellazione Non Consentita");
                    lv_obj_t* m = lv_label_create(g_modal_body);
                    lv_label_set_text(m, why.c_str());
                    lv_obj_set_style_text_color(m, lv_color_hex(0xFFFFFF), 0);
                    lv_obj_set_style_text_font(m, &lv_font_montserrat_14, 0);
                    lv_obj_t* b = lv_btn_create(g_modal_body);
                    lv_obj_set_size(b, 140, 40);
                    lv_obj_t* l = lv_label_create(b);
                    lv_label_set_text(l, "Chiudi");
                    lv_obj_center(l);
                    lv_obj_add_event_cb(b, close_modal, LV_EVENT_CLICKED, nullptr);
                    return;
                }

                // Primo passaggio
                create_modal("Conferma Cancellazione Errori (1/2)");
                lv_obj_t* msg = lv_label_create(g_modal_body);
                lv_label_set_text(msg, "Sei sicuro di voler azzerare la memoria errori della centralina?\n\nQuesta operazione spegnera' la spia Check Engine (MIL).");
                lv_obj_set_style_text_color(msg, lv_color_hex(0xFFFFFF), 0);
                lv_obj_set_style_text_font(msg, &lv_font_montserrat_14, 0);

                lv_obj_t* row = lv_obj_create(g_modal_body);
                lv_obj_remove_style_all(row);
                lv_obj_set_width(row, LV_PCT(100));
                lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
                lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
                lv_obj_set_style_pad_top(row, 16, 0);

                lv_obj_t* btn_cancel = lv_btn_create(row);
                lv_obj_set_size(btn_cancel, 160, 42);
                lv_obj_set_style_bg_color(btn_cancel, lv_color_hex(0x444454), 0);
                lv_obj_t* l_c = lv_label_create(btn_cancel);
                lv_label_set_text(l_c, "Annulla");
                lv_obj_center(l_c);
                lv_obj_add_event_cb(btn_cancel, close_modal, LV_EVENT_CLICKED, nullptr);

                lv_obj_t* btn_ok = lv_btn_create(row);
                lv_obj_set_size(btn_ok, 160, 42);
                lv_obj_set_style_bg_color(btn_ok, lv_color_hex(0xCC0000), 0);
                lv_obj_t* l_ok = lv_label_create(btn_ok);
                lv_label_set_text(l_ok, "Continua");
                lv_obj_center(l_ok);

                lv_obj_add_event_cb(btn_ok, [](lv_event_t* e){
                    close_modal();
                    // Secondo passaggio: spiega cosa viene perso
                    create_modal("Conferma Definitiva (2/2)");
                    lv_obj_t* msg2 = lv_label_create(g_modal_body);
                    lv_label_set_text(msg2,
                        "ATTENZIONE: verranno cancellati anche i dati di freeze frame\n"
                        "e azzerati i monitor di prontezza (readiness).\n"
                        "Finche' non si completano di nuovo guidando, la revisione\n"
                        "puo' non essere superata. Procedere?");
                    lv_label_set_long_mode(msg2, LV_LABEL_LONG_WRAP);
                    lv_obj_set_width(msg2, LV_PCT(100));
                    lv_obj_set_style_text_color(msg2, lv_color_hex(0xFFCC00), 0);
                    lv_obj_set_style_text_font(msg2, &lv_font_montserrat_14, 0);

                    lv_obj_t* row2 = lv_obj_create(g_modal_body);
                    lv_obj_remove_style_all(row2);
                    lv_obj_set_width(row2, LV_PCT(100));
                    lv_obj_set_height(row2, LV_SIZE_CONTENT);
                    lv_obj_set_flex_flow(row2, LV_FLEX_FLOW_ROW);
                    lv_obj_set_flex_align(row2, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
                    lv_obj_set_style_pad_top(row2, 16, 0);

                    lv_obj_t* b_no = lv_btn_create(row2);
                    lv_obj_set_size(b_no, 160, 42);
                    lv_obj_set_style_bg_color(b_no, lv_color_hex(0x444454), 0);
                    lv_obj_t* l_no = lv_label_create(b_no);
                    lv_label_set_text(l_no, "Annulla");
                    lv_obj_center(l_no);
                    lv_obj_add_event_cb(b_no, close_modal, LV_EVENT_CLICKED, nullptr);

                    lv_obj_t* b_yes = lv_btn_create(row2);
                    lv_obj_set_size(b_yes, 160, 42);
                    lv_obj_set_style_bg_color(b_yes, lv_color_hex(0xCC0000), 0);
                    lv_obj_t* l_yes = lv_label_create(b_yes);
                    lv_label_set_text(l_yes, "Cancella ora");
                    lv_obj_center(l_yes);

                    lv_obj_add_event_cb(b_yes, [](lv_event_t* e){
                        close_modal();
                        std::string status_msg;
                        obd::obd().clearDtcCodes(status_msg);  // ricontrolla RPM/velocita'
                        create_modal("Esito Cancellazione");
                        lv_obj_t* res = lv_label_create(g_modal_body);
                        lv_label_set_text(res, status_msg.c_str());
                        lv_obj_set_style_text_color(res, lv_color_hex(0xFFFFFF), 0);
                        lv_obj_set_style_text_font(res, &lv_font_montserrat_14, 0);

                        lv_obj_t* btn_close = lv_btn_create(g_modal_body);
                        lv_obj_set_size(btn_close, 140, 40);
                        lv_obj_set_style_bg_color(btn_close, lv_color_hex(0x00CC66), 0);
                        lv_obj_set_style_pad_top(btn_close, 14, 0);
                        lv_obj_t* l_cl = lv_label_create(btn_close);
                        lv_label_set_text(l_cl, "Chiudi");
                        lv_obj_center(l_cl);
                        lv_obj_add_event_cb(btn_close, close_modal, LV_EVENT_CLICKED, nullptr);
                    }, LV_EVENT_CLICKED, nullptr);
                }, LV_EVENT_CLICKED, nullptr);
            }, LV_EVENT_CLICKED, nullptr);
            break;
        }

        // === 8: INFO SISTEMA & STATO ===
        case 8: {
            // === STATO CONNESSIONE OBD & DIAGNOSTICA (Spostato dal vecchio pannello Stato) ===
            lv_obj_t* c_stat = make_card_group(g_content_panel, "Stato Connessione Adattatore & ECU", LV_SYMBOL_WIFI);

            g_lbl_stat_device = lv_label_create(c_stat);
            lv_obj_set_style_text_font(g_lbl_stat_device, &lv_font_montserrat_16, 0);
            lv_obj_set_style_text_color(g_lbl_stat_device, lv_color_hex(0xF8F8F8), 0);
            lv_label_set_text(g_lbl_stat_device, "Dispositivo: In ricerca...");

            lv_obj_t* st_hdr = lv_label_create(c_stat);
            lv_obj_set_style_text_font(st_hdr, &lv_font_montserrat_12, 0);
            lv_obj_set_style_text_color(st_hdr, lv_color_hex(0x9A9AA6), 0);
            lv_label_set_text(st_hdr, "Stato di Comunicazione OBD-II:");

            g_lbl_stat_state = lv_label_create(c_stat);
            lv_obj_set_style_text_font(g_lbl_stat_state, &lv_font_montserrat_22, 0);
            lv_obj_set_style_text_color(g_lbl_stat_state, lv_color_hex(0x9A9AA6), 0);
            lv_label_set_text(g_lbl_stat_state, "DISCONNESSO");

            // Row interna per OK ed ERR
            lv_obj_t* cnt_row = lv_obj_create(c_stat);
            lv_obj_remove_style_all(cnt_row);
            lv_obj_set_size(cnt_row, LV_PCT(100), LV_SIZE_CONTENT);
            lv_obj_set_flex_flow(cnt_row, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(cnt_row, LV_FLEX_ALIGN_SPACE_AROUND, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_set_style_pad_ver(cnt_row, 8, 0);

            // Box OK
            lv_obj_t* box_ok = lv_obj_create(cnt_row);
            lv_obj_remove_style_all(box_ok);
            lv_obj_set_flex_flow(box_ok, LV_FLEX_FLOW_COLUMN);
            lv_obj_t* lbl_ok_t = lv_label_create(box_ok);
            lv_obj_set_style_text_font(lbl_ok_t, &lv_font_montserrat_12, 0);
            lv_obj_set_style_text_color(lbl_ok_t, lv_color_hex(0x9A9AA6), 0);
            lv_label_set_text(lbl_ok_t, "Letture OK");

            g_lbl_stat_ok = lv_label_create(box_ok);
            lv_obj_set_style_text_font(g_lbl_stat_ok, &lv_font_montserrat_24, 0);
            lv_obj_set_style_text_color(g_lbl_stat_ok, lv_color_hex(0x30CC60), 0);
            lv_label_set_text_fmt(g_lbl_stat_ok, "%lu", static_cast<unsigned long>(abarth::obd::obd().successfulReads()));

            // Box ERR
            lv_obj_t* box_err = lv_obj_create(cnt_row);
            lv_obj_remove_style_all(box_err);
            lv_obj_set_flex_flow(box_err, LV_FLEX_FLOW_COLUMN);
            lv_obj_t* lbl_err_t = lv_label_create(box_err);
            lv_obj_set_style_text_font(lbl_err_t, &lv_font_montserrat_12, 0);
            lv_obj_set_style_text_color(lbl_err_t, lv_color_hex(0x9A9AA6), 0);
            lv_label_set_text(lbl_err_t, "Errori / Retry");

            g_lbl_stat_err = lv_label_create(box_err);
            lv_obj_set_style_text_font(g_lbl_stat_err, &lv_font_montserrat_24, 0);
            lv_obj_set_style_text_color(g_lbl_stat_err, lv_color_hex(0xFF5252), 0);
            lv_label_set_text_fmt(g_lbl_stat_err, "%lu", static_cast<unsigned long>(abarth::obd::obd().failedReads()));

            g_lbl_stat_lasterr = lv_label_create(c_stat);
            lv_obj_set_style_text_font(g_lbl_stat_lasterr, &lv_font_montserrat_12, 0);
            lv_obj_set_style_text_color(g_lbl_stat_lasterr, lv_color_hex(0x9A9AA6), 0);
            lv_label_set_text(g_lbl_stat_lasterr, "Nessun errore rilevato");

            // Card Guida
            lv_obj_t* c_guide = make_card_group(g_content_panel, "Guida Monitoraggio Adattatore & ECU", LV_SYMBOL_FILE);
            lv_obj_t* hint = lv_label_create(c_guide);
            lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, 0);
            lv_obj_set_style_text_color(hint, lv_color_hex(0xDEDEDE), 0);
            lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
            lv_obj_set_width(hint, LV_PCT(100));
            lv_label_set_text(hint,
                "• Disconnesso: Adattatore OBD non collegato o disattivato.\n"
                "• Attesa...: Comunicazione avviata, dati PID in arrivo dall'auto.\n"
                "• Non supportato: PID non risposto dalla centralina ECU Abarth.\n"
                "• Supporto Dongle: Compatibile con dispositivi BLE (vLinker, OBDLink CX, ELM327) e WiFi.");

            // Hardware Info
            lv_obj_t* c1 = make_card_group(g_content_panel, "Informazioni Hardware & Firmware", LV_SYMBOL_SETTINGS);

            g_lbl_sysinfo = lv_label_create(c1);
            lv_obj_set_style_text_color(g_lbl_sysinfo, lv_color_hex(0x00FFFF), 0);
            lv_obj_set_style_text_font(g_lbl_sysinfo, &lv_font_montserrat_14, 0);
            lv_label_set_long_mode(g_lbl_sysinfo, LV_LABEL_LONG_WRAP);
            lv_obj_set_width(g_lbl_sysinfo, LV_PCT(100));
            lv_label_set_text(g_lbl_sysinfo, "Caricamento statistiche sistema...");

            // Real-Time Clock & Batteria Tampone CR1220 (Link Rapido alla Categoria Dedicata)
            lv_obj_t* c_rtc = make_card_group(g_content_panel, "Orologio Hardware RTC & Batteria", LV_SYMBOL_REFRESH);

            lv_obj_t* lbl_rtc_desc = lv_label_create(c_rtc);
            lv_obj_set_style_text_color(lbl_rtc_desc, lv_color_hex(0x38BDF8), 0);
            lv_obj_set_style_text_font(lbl_rtc_desc, &lv_font_montserrat_14, 0);
            lv_label_set_long_mode(lbl_rtc_desc, LV_LABEL_LONG_WRAP);
            lv_obj_set_width(lbl_rtc_desc, LV_PCT(100));
            auto& rtc_ref = abarth::storage::rtc();
            char rtc_quick[256];
            std::snprintf(rtc_quick, sizeof(rtc_quick),
                "Chip: %s [0x%02X]  |  Batteria: %s\n"
                "Data e Ora attuali: %s\n"
                "Per regolare ore, minuti, data e opzioni apri la categoria dedicata:",
                rtc_ref.getChipName(), rtc_ref.getI2cAddress(),
                rtc_ref.isBatteryOk() ? "Presente / Carica (OK)" : "Bassa Tensione",
                rtc_ref.getFormattedTime().c_str()
            );
            lv_label_set_text(lbl_rtc_desc, rtc_quick);

            lv_obj_t* btn_open_rtc = lv_btn_create(c_rtc);
            lv_obj_set_width(btn_open_rtc, LV_PCT(100));
            lv_obj_set_height(btn_open_rtc, 44);
            lv_obj_set_style_bg_color(btn_open_rtc, lv_color_hex(0x0284C7), 0);
            lv_obj_set_style_radius(btn_open_rtc, 10, 0);
            lv_obj_t* lbl_ortc = lv_label_create(btn_open_rtc);
            lv_label_set_text(lbl_ortc, LV_SYMBOL_REFRESH " Apri Categoria 'Data, Ora & RTC'");
            lv_obj_set_style_text_font(lbl_ortc, &lv_font_montserrat_14, 0);
            lv_obj_center(lbl_ortc);
            lv_obj_add_event_cb(btn_open_rtc, [](lv_event_t*) {
                open_category(9, 0);
            }, LV_EVENT_CLICKED, nullptr);

            // Storage MicroSD & Telemetria (Link Rapido alla Categoria Dedicata)
            lv_obj_t* c_sd_quick = make_card_group(g_content_panel, "Storage MicroSD & Telemetria", LV_SYMBOL_SD_CARD);
            lv_obj_t* lbl_sd_qdesc = lv_label_create(c_sd_quick);
            lv_obj_set_style_text_color(lbl_sd_qdesc, lv_color_hex(0x10B981), 0);
            lv_obj_set_style_text_font(lbl_sd_qdesc, &lv_font_montserrat_14, 0);
            lv_label_set_long_mode(lbl_sd_qdesc, LV_LABEL_LONG_WRAP);
            lv_obj_set_width(lbl_sd_qdesc, LV_PCT(100));
            auto& sd_ref = abarth::storage::sd();
            char sd_quick[256];
            if (sd_ref.isMounted()) {
                std::snprintf(sd_quick, sizeof(sd_quick),
                    "Stato: Scheda Rilevata e Montata su /sdcard (S:/)\n"
                    "Capacita': %llu MB Totali  |  %llu MB Liberi\n"
                    "Per gestire log corse, icone personalizzate o benchmark di velocita':",
                    (unsigned long long)sd_ref.getTotalSizeMB(),
                    (unsigned long long)sd_ref.getFreeSizeMB()
                );
            } else {
                std::snprintf(sd_quick, sizeof(sd_quick),
                    "Stato: Nessuna scheda inserita o montata.\n"
                    "Slot SDMMC 4-bit (GPIO 39-44). Inserire una MicroSD FAT32."
                );
            }
            lv_label_set_text(lbl_sd_qdesc, sd_quick);

            lv_obj_t* btn_open_sd = lv_btn_create(c_sd_quick);
            lv_obj_set_width(btn_open_sd, LV_PCT(100));
            lv_obj_set_height(btn_open_sd, 44);
            lv_obj_set_style_bg_color(btn_open_sd, lv_color_hex(0x059669), 0);
            lv_obj_set_style_radius(btn_open_sd, 10, 0);
            lv_obj_t* lbl_osd = lv_label_create(btn_open_sd);
            lv_label_set_text(lbl_osd, LV_SYMBOL_SD_CARD " Apri Categoria 'Memoria MicroSD'");
            lv_obj_set_style_text_font(lbl_osd, &lv_font_montserrat_14, 0);
            lv_obj_center(lbl_osd);
            lv_obj_add_event_cb(btn_open_sd, [](lv_event_t*) {
                open_category(10, 0);
            }, LV_EVENT_CLICKED, nullptr);

            // Diagnostics Info
            lv_obj_t* c2 = make_card_group(g_content_panel, "Diagnostica Letture CAN/OBD", LV_SYMBOL_LIST);

            g_lbl_counters_info = lv_label_create(c2);
            lv_obj_set_style_text_color(g_lbl_counters_info, lv_color_hex(0xDEDEDE), 0);
            lv_obj_set_style_text_font(g_lbl_counters_info, &lv_font_montserrat_14, 0);
            lv_label_set_long_mode(g_lbl_counters_info, LV_LABEL_LONG_WRAP);
            lv_obj_set_width(g_lbl_counters_info, LV_PCT(100));
            lv_label_set_text(g_lbl_counters_info, "Letture OK: -- | Errori: --");

            // Aggiornamento Firmware MicroSD
            lv_obj_t* c_ota = make_card_group(g_content_panel, "Aggiornamento Firmware MicroSD (Offline)", LV_SYMBOL_DOWNLOAD);
            make_sub_label(c_ota, "Cerca file 'update.bin' o 'firmware.bin' nella root della MicroSD per aggiornare il cruscotto senza cavi USB:");

            lv_obj_t* btn_check_ota = lv_btn_create(c_ota);
            lv_obj_set_width(btn_check_ota, LV_PCT(100));
            lv_obj_set_height(btn_check_ota, 44);
            lv_obj_set_style_bg_color(btn_check_ota, lv_color_hex(0x0284C7), 0);
            lv_obj_set_style_radius(btn_check_ota, 10, 0);
            lv_obj_t* lbl_cota = lv_label_create(btn_check_ota);
            lv_label_set_text(lbl_cota, LV_SYMBOL_REFRESH " Cerca & Applica Aggiornamento da MicroSD");
            lv_obj_set_style_text_font(lbl_cota, &lv_font_montserrat_14, 0);
            lv_obj_center(lbl_cota);

            lv_obj_add_event_cb(btn_check_ota, [](lv_event_t*) {
                struct stat st;
                bool found = (stat("/sdcard/update.bin", &st) == 0 && st.st_size > 100000) ||
                             (stat("/sdcard/firmware.bin", &st) == 0 && st.st_size > 100000);
                if (!found) {
                    create_modal("Nessun Aggiornamento Trovato");
                    lv_obj_t* msg = lv_label_create(g_modal_body);
                    lv_label_set_text(msg, "Nessun file 'update.bin' o 'firmware.bin' valido trovato sulla MicroSD.\n\nCopia il file compilato nella root della scheda e riprova.");
                    lv_obj_set_style_text_color(msg, lv_color_hex(0xFFFFFF), 0);
                    lv_obj_set_style_text_font(msg, &lv_font_montserrat_14, 0);

                    lv_obj_t* b_ok = lv_btn_create(g_modal_body);
                    lv_obj_set_size(b_ok, 160, 42);
                    lv_obj_set_style_bg_color(b_ok, lv_color_hex(0x334155), 0);
                    lv_obj_set_style_pad_top(b_ok, 12, 0);
                    lv_obj_t* l_ok = lv_label_create(b_ok);
                    lv_label_set_text(l_ok, "Ho capito");
                    lv_obj_center(l_ok);
                    lv_obj_add_event_cb(b_ok, close_modal, LV_EVENT_CLICKED, nullptr);
                } else {
                    create_modal("Conferma Installazione Firmware");
                    lv_obj_t* msg = lv_label_create(g_modal_body);
                    lv_label_set_text(msg, "Rilevato file firmware valido sulla MicroSD!\n\nVuoi avviare l'installazione nella partizione OTA opposta?\nIl cruscotto si riavvierà automaticamente.");
                    lv_obj_set_style_text_color(msg, lv_color_hex(0xFFFFFF), 0);
                    lv_obj_set_style_text_font(msg, &lv_font_montserrat_14, 0);

                    lv_obj_t* row = lv_obj_create(g_modal_body);
                    lv_obj_remove_style_all(row);
                    lv_obj_set_width(row, LV_PCT(100));
                    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
                    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
                    lv_obj_set_style_pad_top(row, 14, 0);

                    lv_obj_t* b_c = lv_btn_create(row);
                    lv_obj_set_size(b_c, 160, 42);
                    lv_obj_set_style_bg_color(b_c, lv_color_hex(0x444454), 0);
                    lv_obj_t* l_c = lv_label_create(b_c);
                    lv_label_set_text(l_c, "Annulla");
                    lv_obj_center(l_c);
                    lv_obj_add_event_cb(b_c, close_modal, LV_EVENT_CLICKED, nullptr);

                    lv_obj_t* b_up = lv_btn_create(row);
                    lv_obj_set_size(b_up, 160, 42);
                    lv_obj_set_style_bg_color(b_up, lv_color_hex(0x0284C7), 0);
                    lv_obj_t* l_up = lv_label_create(b_up);
                    lv_label_set_text(l_up, LV_SYMBOL_DOWNLOAD " Installa Ora");
                    lv_obj_center(l_up);

                    lv_obj_add_event_cb(b_up, [](lv_event_t*) {
                        close_modal();
                        create_modal("Aggiornamento in corso...");
                        lv_obj_t* pmsg = lv_label_create(g_modal_body);
                        lv_label_set_text(pmsg, "Scrittura flash OTA in corso... Non spegnere l'auto.");
                        lv_obj_set_style_text_color(pmsg, lv_color_hex(0x38BDF8), 0);
                        lv_obj_set_style_text_font(pmsg, &lv_font_montserrat_14, 0);
                        lv_refr_now(nullptr);

                        abarth::ota::ota().checkAndApplySdUpdate([](int pct, const char* text){
                            ESP_LOGI(TAG, "OTA UI: %d%% - %s", pct, text);
                        });
                    }, LV_EVENT_CLICKED, nullptr);
                }
            }, LV_EVENT_CLICKED, nullptr);

            // Reboot System
            lv_obj_t* c3 = make_card_group(g_content_panel, "Gestione Alimentazione", LV_SYMBOL_POWER);

            lv_obj_t* btn_reboot = lv_btn_create(c3);
            lv_obj_set_width(btn_reboot, LV_PCT(100));
            lv_obj_set_height(btn_reboot, 48);
            lv_obj_set_style_bg_color(btn_reboot, lv_color_hex(0x660000), 0);
            lv_obj_set_style_radius(btn_reboot, 10, 0);
            lv_obj_t* lbl_rb = lv_label_create(btn_reboot);
            lv_label_set_text(lbl_rb, LV_SYMBOL_POWER " Riavvia Dispositivo");
            lv_obj_set_style_text_color(lbl_rb, lv_color_hex(0xFFFFFF), 0);
            lv_obj_set_style_text_font(lbl_rb, &lv_font_montserrat_14, 0);
            lv_obj_center(lbl_rb);
            lv_obj_add_event_cb(btn_reboot, reboot_cb, LV_EVENT_CLICKED, nullptr);
            break;
        }

        // === 9: DATA, ORA & RTC ===
        case 9: {
            // Card 1: Display Orologio & Stato Hardware
            lv_obj_t* c1 = make_card_group(g_content_panel, "Orologio Real-Time & Batteria", LV_SYMBOL_REFRESH);

            // Digital Clock Panel
            lv_obj_t* clock_panel = lv_obj_create(c1);
            lv_obj_set_width(clock_panel, LV_PCT(100));
            lv_obj_set_height(clock_panel, LV_SIZE_CONTENT);
            lv_obj_set_style_bg_color(clock_panel, lv_color_hex(0x0B0F19), 0);
            lv_obj_set_style_bg_opa(clock_panel, LV_OPA_COVER, 0);
            lv_obj_set_style_border_color(clock_panel, lv_color_hex(0x1E293B), 0);
            lv_obj_set_style_border_width(clock_panel, 1, 0);
            lv_obj_set_style_radius(clock_panel, 12, 0);
            lv_obj_set_style_pad_all(clock_panel, 14, 0);
            lv_obj_set_flex_flow(clock_panel, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_flex_align(clock_panel, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

            // Grandi cifre digitali ora HH:MM:SS
            g_lbl_rtc_time_display = lv_label_create(clock_panel);
            lv_obj_set_style_text_color(g_lbl_rtc_time_display, lv_color_hex(0x38BDF8), 0);
            lv_obj_set_style_text_font(g_lbl_rtc_time_display, &lv_font_montserrat_36, 0);
            lv_label_set_text(g_lbl_rtc_time_display, "-- : -- : --");

            // Riga data completa
            g_lbl_rtc_date_display = lv_label_create(clock_panel);
            lv_obj_set_style_text_color(g_lbl_rtc_date_display, lv_color_hex(0xF1F5F9), 0);
            lv_obj_set_style_text_font(g_lbl_rtc_date_display, &lv_font_montserrat_16, 0);
            lv_label_set_text(g_lbl_rtc_date_display, "--, -- -- ----");

            // Info dettagliate chip & orologio
            g_lbl_rtc_info = lv_label_create(c1);
            lv_obj_set_style_text_color(g_lbl_rtc_info, lv_color_hex(0x94A3B8), 0);
            lv_obj_set_style_text_font(g_lbl_rtc_info, &lv_font_montserrat_14, 0);
            lv_label_set_long_mode(g_lbl_rtc_info, LV_LABEL_LONG_WRAP);
            lv_obj_set_width(g_lbl_rtc_info, LV_PCT(100));
            lv_label_set_text(g_lbl_rtc_info, "Caricamento stato RTC...");

            // Card 2: Stato Batteria Backup RTC (CR1220)
            auto& rtc_ref = abarth::storage::rtc();
            const bool batt_ok = rtc_ref.isBatteryOk();

            lv_obj_t* c_batt = make_card_group(g_content_panel, "Stato Batteria Backup RTC (CR1220)", batt_ok ? LV_SYMBOL_BATTERY_FULL : LV_SYMBOL_BATTERY_EMPTY);

            // Banner visivo ad alto contrasto per la batteria
            lv_obj_t* b_card = lv_obj_create(c_batt);
            lv_obj_remove_style_all(b_card);
            lv_obj_set_width(b_card, LV_PCT(100));
            lv_obj_set_height(b_card, LV_SIZE_CONTENT);
            lv_obj_set_style_radius(b_card, 10, 0);
            lv_obj_set_style_pad_all(b_card, 14, 0);
            lv_obj_set_flex_flow(b_card, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(b_card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_set_style_pad_gap(b_card, 14, 0);

            if (batt_ok) {
                lv_obj_set_style_bg_color(b_card, lv_color_hex(0x064E3B), 0);
                lv_obj_set_style_bg_opa(b_card, LV_OPA_COVER, 0);
                lv_obj_set_style_border_color(b_card, lv_color_hex(0x10B981), 0);
                lv_obj_set_style_border_width(b_card, 2, 0);
            } else {
                lv_obj_set_style_bg_color(b_card, lv_color_hex(0x450A0A), 0);
                lv_obj_set_style_bg_opa(b_card, LV_OPA_COVER, 0);
                lv_obj_set_style_border_color(b_card, lv_color_hex(0xEF4444), 0);
                lv_obj_set_style_border_width(b_card, 2, 0);
            }

            lv_obj_t* b_ico = lv_label_create(b_card);
            lv_label_set_text(b_ico, batt_ok ? LV_SYMBOL_BATTERY_FULL : LV_SYMBOL_BATTERY_EMPTY);
            lv_obj_set_style_text_color(b_ico, lv_color_hex(batt_ok ? 0x34D399 : 0xF87171), 0);
            lv_obj_set_style_text_font(b_ico, &lv_font_montserrat_28, 0);

            lv_obj_t* b_text_col = lv_obj_create(b_card);
            lv_obj_remove_style_all(b_text_col);
            lv_obj_set_flex_grow(b_text_col, 1);
            lv_obj_set_flex_flow(b_text_col, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_style_pad_gap(b_text_col, 4, 0);

            lv_obj_t* b_title = lv_label_create(b_text_col);
            lv_label_set_text(b_title, batt_ok ? "CR1220: ATTIVA E CARICA (OK)" : "CR1220: TENSIONE BASSA O ASSENTE");
            lv_obj_set_style_text_color(b_title, lv_color_hex(batt_ok ? 0xA7F3D0 : 0xFECACA), 0);
            lv_obj_set_style_text_font(b_title, &lv_font_montserrat_16, 0);

            lv_obj_t* b_sub = lv_label_create(b_text_col);
            lv_label_set_long_mode(b_sub, LV_LABEL_LONG_WRAP);
            lv_obj_set_width(b_sub, LV_PCT(100));
            lv_label_set_text(b_sub, batt_ok ?
                "Tensione nominale corretta (~3.0V). L'orologio hardware mantiene orario e data esatti anche a cruscotto spento." :
                "Rilevata caduta di tensione sul circuito RTC (VLF flag attivo). Inserire una batteria CR1220 carica per conservare data e ora a quadro spento.");
            lv_obj_set_style_text_color(b_sub, lv_color_hex(batt_ok ? 0xD1FAE5 : 0xFEE2E2), 0);
            lv_obj_set_style_text_font(b_sub, &lv_font_montserrat_12, 0);

            // Dettagli tecnici batteria
            lv_obj_t* b_details = lv_label_create(c_batt);
            lv_obj_set_style_text_color(b_details, lv_color_hex(0xCBD5E1), 0);
            lv_obj_set_style_text_font(b_details, &lv_font_montserrat_12, 0);
            lv_label_set_long_mode(b_details, LV_LABEL_LONG_WRAP);
            lv_obj_set_width(b_details, LV_PCT(100));
            lv_label_set_text(b_details,
                "• Tipo Batteria: Litio a bottone 3V (Modello standard CR1220)\n"
                "• Slot Hardware: Alloggiamento a molla sul retro del modulo JC1060P470C\n"
                "• Monitoraggio Hardware: Flag VLF su registro 0x1D del chip Epson RX-8130CE\n"
                "• Durata Stimata: 3-5 anni grazie all'assorbimento ultra-basso di 160 nA");

            // Card 3: Regolazione Orario (Ore, Minuti, Secondi)
            lv_obj_t* c2 = make_card_group(g_content_panel, "Regolazione Orario", LV_SYMBOL_SETTINGS);
            make_sub_label(c2, "Modifica ore e minuti in tempo reale con salvataggio immediato nell'RTC:");

            // --- Riga Ore ---
            lv_obj_t* row_h = lv_obj_create(c2);
            lv_obj_remove_style_all(row_h);
            lv_obj_set_width(row_h, LV_PCT(100));
            lv_obj_set_height(row_h, 48);
            lv_obj_set_flex_flow(row_h, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(row_h, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_set_style_pad_gap(row_h, 10, 0);

            lv_obj_t* lbl_h_title = lv_label_create(row_h);
            lv_obj_set_width(lbl_h_title, 85);
            lv_label_set_text(lbl_h_title, "Ore:");
            lv_obj_set_style_text_color(lbl_h_title, lv_color_hex(0xFFFFFF), 0);
            lv_obj_set_style_text_font(lbl_h_title, &lv_font_montserrat_16, 0);

            lv_obj_t* btn_m1h = lv_btn_create(row_h);
            lv_obj_set_size(btn_m1h, 85, 42);
            lv_obj_set_style_bg_color(btn_m1h, lv_color_hex(0x1E293B), 0);
            lv_obj_set_style_border_color(btn_m1h, lv_color_hex(0x334155), 0);
            lv_obj_set_style_border_width(btn_m1h, 1, 0);
            lv_obj_set_style_radius(btn_m1h, 8, 0);
            lv_obj_t* lm1h = lv_label_create(btn_m1h);
            lv_label_set_text(lm1h, "-1 Ora");
            lv_obj_set_style_text_font(lm1h, &lv_font_montserrat_14, 0);
            lv_obj_center(lm1h);
            lv_obj_add_event_cb(btn_m1h, [](lv_event_t*) {
                abarth::storage::rtc().adjustHours(-1);
                refresh_rtc_ui();
            }, LV_EVENT_CLICKED, nullptr);

            lv_obj_t* cont_h_val = lv_obj_create(row_h);
            lv_obj_set_size(cont_h_val, 70, 42);
            lv_obj_set_style_bg_color(cont_h_val, lv_color_hex(0x0F172A), 0);
            lv_obj_set_style_border_color(cont_h_val, lv_color_hex(0x38BDF8), 0);
            lv_obj_set_style_border_width(cont_h_val, 1, 0);
            lv_obj_set_style_radius(cont_h_val, 8, 0);
            g_lbl_rtc_hour_val = lv_label_create(cont_h_val);
            lv_obj_set_style_text_color(g_lbl_rtc_hour_val, lv_color_hex(0x38BDF8), 0);
            lv_obj_set_style_text_font(g_lbl_rtc_hour_val, &lv_font_montserrat_22, 0);
            lv_label_set_text(g_lbl_rtc_hour_val, "--");
            lv_obj_center(g_lbl_rtc_hour_val);

            lv_obj_t* btn_p1h = lv_btn_create(row_h);
            lv_obj_set_size(btn_p1h, 85, 42);
            lv_obj_set_style_bg_color(btn_p1h, lv_color_hex(0x1E293B), 0);
            lv_obj_set_style_border_color(btn_p1h, lv_color_hex(0x334155), 0);
            lv_obj_set_style_border_width(btn_p1h, 1, 0);
            lv_obj_set_style_radius(btn_p1h, 8, 0);
            lv_obj_t* lp1h = lv_label_create(btn_p1h);
            lv_label_set_text(lp1h, "+1 Ora");
            lv_obj_set_style_text_font(lp1h, &lv_font_montserrat_14, 0);
            lv_obj_center(lp1h);
            lv_obj_add_event_cb(btn_p1h, [](lv_event_t*) {
                abarth::storage::rtc().adjustHours(1);
                refresh_rtc_ui();
            }, LV_EVENT_CLICKED, nullptr);

            // --- Riga Minuti ---
            lv_obj_t* row_m = lv_obj_create(c2);
            lv_obj_remove_style_all(row_m);
            lv_obj_set_width(row_m, LV_PCT(100));
            lv_obj_set_height(row_m, 48);
            lv_obj_set_flex_flow(row_m, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(row_m, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_set_style_pad_gap(row_m, 8, 0);

            lv_obj_t* lbl_m_title = lv_label_create(row_m);
            lv_obj_set_width(lbl_m_title, 85);
            lv_label_set_text(lbl_m_title, "Minuti:");
            lv_obj_set_style_text_color(lbl_m_title, lv_color_hex(0xFFFFFF), 0);
            lv_obj_set_style_text_font(lbl_m_title, &lv_font_montserrat_16, 0);

            lv_obj_t* btn_m5m = lv_btn_create(row_m);
            lv_obj_set_size(btn_m5m, 62, 42);
            lv_obj_set_style_bg_color(btn_m5m, lv_color_hex(0x1E293B), 0);
            lv_obj_set_style_border_color(btn_m5m, lv_color_hex(0x334155), 0);
            lv_obj_set_style_border_width(btn_m5m, 1, 0);
            lv_obj_set_style_radius(btn_m5m, 8, 0);
            lv_obj_t* lm5m = lv_label_create(btn_m5m);
            lv_label_set_text(lm5m, "-5m");
            lv_obj_set_style_text_font(lm5m, &lv_font_montserrat_14, 0);
            lv_obj_center(lm5m);
            lv_obj_add_event_cb(btn_m5m, [](lv_event_t*) {
                abarth::storage::rtc().adjustMinutes(-5);
                refresh_rtc_ui();
            }, LV_EVENT_CLICKED, nullptr);

            lv_obj_t* btn_m1m = lv_btn_create(row_m);
            lv_obj_set_size(btn_m1m, 62, 42);
            lv_obj_set_style_bg_color(btn_m1m, lv_color_hex(0x1E293B), 0);
            lv_obj_set_style_border_color(btn_m1m, lv_color_hex(0x334155), 0);
            lv_obj_set_style_border_width(btn_m1m, 1, 0);
            lv_obj_set_style_radius(btn_m1m, 8, 0);
            lv_obj_t* lm1m = lv_label_create(btn_m1m);
            lv_label_set_text(lm1m, "-1m");
            lv_obj_set_style_text_font(lm1m, &lv_font_montserrat_14, 0);
            lv_obj_center(lm1m);
            lv_obj_add_event_cb(btn_m1m, [](lv_event_t*) {
                abarth::storage::rtc().adjustMinutes(-1);
                refresh_rtc_ui();
            }, LV_EVENT_CLICKED, nullptr);

            lv_obj_t* cont_m_val = lv_obj_create(row_m);
            lv_obj_set_size(cont_m_val, 70, 42);
            lv_obj_set_style_bg_color(cont_m_val, lv_color_hex(0x0F172A), 0);
            lv_obj_set_style_border_color(cont_m_val, lv_color_hex(0x38BDF8), 0);
            lv_obj_set_style_border_width(cont_m_val, 1, 0);
            lv_obj_set_style_radius(cont_m_val, 8, 0);
            g_lbl_rtc_min_val = lv_label_create(cont_m_val);
            lv_obj_set_style_text_color(g_lbl_rtc_min_val, lv_color_hex(0x38BDF8), 0);
            lv_obj_set_style_text_font(g_lbl_rtc_min_val, &lv_font_montserrat_22, 0);
            lv_label_set_text(g_lbl_rtc_min_val, "--");
            lv_obj_center(g_lbl_rtc_min_val);

            lv_obj_t* btn_p1m = lv_btn_create(row_m);
            lv_obj_set_size(btn_p1m, 62, 42);
            lv_obj_set_style_bg_color(btn_p1m, lv_color_hex(0x1E293B), 0);
            lv_obj_set_style_border_color(btn_p1m, lv_color_hex(0x334155), 0);
            lv_obj_set_style_border_width(btn_p1m, 1, 0);
            lv_obj_set_style_radius(btn_p1m, 8, 0);
            lv_obj_t* lp1m = lv_label_create(btn_p1m);
            lv_label_set_text(lp1m, "+1m");
            lv_obj_set_style_text_font(lp1m, &lv_font_montserrat_14, 0);
            lv_obj_center(lp1m);
            lv_obj_add_event_cb(btn_p1m, [](lv_event_t*) {
                abarth::storage::rtc().adjustMinutes(1);
                refresh_rtc_ui();
            }, LV_EVENT_CLICKED, nullptr);

            lv_obj_t* btn_p5m = lv_btn_create(row_m);
            lv_obj_set_size(btn_p5m, 62, 42);
            lv_obj_set_style_bg_color(btn_p5m, lv_color_hex(0x1E293B), 0);
            lv_obj_set_style_border_color(btn_p5m, lv_color_hex(0x334155), 0);
            lv_obj_set_style_border_width(btn_p5m, 1, 0);
            lv_obj_set_style_radius(btn_p5m, 8, 0);
            lv_obj_t* lp5m = lv_label_create(btn_p5m);
            lv_label_set_text(lp5m, "+5m");
            lv_obj_set_style_text_font(lp5m, &lv_font_montserrat_14, 0);
            lv_obj_center(lp5m);
            lv_obj_add_event_cb(btn_p5m, [](lv_event_t*) {
                abarth::storage::rtc().adjustMinutes(5);
                refresh_rtc_ui();
            }, LV_EVENT_CLICKED, nullptr);

            // --- Riga Secondi ---
            lv_obj_t* row_s = lv_obj_create(c2);
            lv_obj_remove_style_all(row_s);
            lv_obj_set_width(row_s, LV_PCT(100));
            lv_obj_set_height(row_s, 44);
            lv_obj_set_flex_flow(row_s, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(row_s, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_set_style_pad_gap(row_s, 10, 0);

            lv_obj_t* lbl_s_title = lv_label_create(row_s);
            lv_obj_set_width(lbl_s_title, 85);
            lv_label_set_text(lbl_s_title, "Secondi:");
            lv_obj_set_style_text_color(lbl_s_title, lv_color_hex(0xFFFFFF), 0);
            lv_obj_set_style_text_font(lbl_s_title, &lv_font_montserrat_16, 0);

            lv_obj_t* btn_szero = lv_btn_create(row_s);
            lv_obj_set_flex_grow(btn_szero, 1);
            lv_obj_set_height(btn_szero, 42);
            lv_obj_set_style_bg_color(btn_szero, lv_color_hex(0x334155), 0);
            lv_obj_set_style_radius(btn_szero, 8, 0);
            lv_obj_t* lszero = lv_label_create(btn_szero);
            lv_label_set_text(lszero, LV_SYMBOL_REFRESH " Azzera Secondi (:00) per Allineamento");
            lv_obj_set_style_text_font(lszero, &lv_font_montserrat_14, 0);
            lv_obj_center(lszero);
            lv_obj_add_event_cb(btn_szero, [](lv_event_t*) {
                abarth::storage::rtc().resetSeconds();
                refresh_rtc_ui();
            }, LV_EVENT_CLICKED, nullptr);

            // Card 3: Regolazione Calendario (Giorno, Mese, Anno)
            lv_obj_t* c3 = make_card_group(g_content_panel, "Regolazione Calendario", LV_SYMBOL_DIRECTORY);
            make_sub_label(c3, "Modifica giorno, mese e anno del calendario:");

            // --- Riga Giorno ---
            lv_obj_t* row_d = lv_obj_create(c3);
            lv_obj_remove_style_all(row_d);
            lv_obj_set_width(row_d, LV_PCT(100));
            lv_obj_set_height(row_d, 48);
            lv_obj_set_flex_flow(row_d, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(row_d, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_set_style_pad_gap(row_d, 10, 0);

            lv_obj_t* lbl_d_title = lv_label_create(row_d);
            lv_obj_set_width(lbl_d_title, 85);
            lv_label_set_text(lbl_d_title, "Giorno:");
            lv_obj_set_style_text_color(lbl_d_title, lv_color_hex(0xFFFFFF), 0);
            lv_obj_set_style_text_font(lbl_d_title, &lv_font_montserrat_16, 0);

            lv_obj_t* btn_m1d = lv_btn_create(row_d);
            lv_obj_set_size(btn_m1d, 100, 42);
            lv_obj_set_style_bg_color(btn_m1d, lv_color_hex(0x1E293B), 0);
            lv_obj_set_style_border_color(btn_m1d, lv_color_hex(0x334155), 0);
            lv_obj_set_style_border_width(btn_m1d, 1, 0);
            lv_obj_set_style_radius(btn_m1d, 8, 0);
            lv_obj_t* lm1d = lv_label_create(btn_m1d);
            lv_label_set_text(lm1d, "-1 Giorno");
            lv_obj_set_style_text_font(lm1d, &lv_font_montserrat_14, 0);
            lv_obj_center(lm1d);
            lv_obj_add_event_cb(btn_m1d, [](lv_event_t*) {
                abarth::storage::rtc().adjustDays(-1);
                refresh_rtc_ui();
            }, LV_EVENT_CLICKED, nullptr);

            lv_obj_t* cont_d_val = lv_obj_create(row_d);
            lv_obj_set_size(cont_d_val, 70, 42);
            lv_obj_set_style_bg_color(cont_d_val, lv_color_hex(0x0F172A), 0);
            lv_obj_set_style_border_color(cont_d_val, lv_color_hex(0x34D399), 0);
            lv_obj_set_style_border_width(cont_d_val, 1, 0);
            lv_obj_set_style_radius(cont_d_val, 8, 0);
            g_lbl_rtc_day_val = lv_label_create(cont_d_val);
            lv_obj_set_style_text_color(g_lbl_rtc_day_val, lv_color_hex(0x34D399), 0);
            lv_obj_set_style_text_font(g_lbl_rtc_day_val, &lv_font_montserrat_22, 0);
            lv_label_set_text(g_lbl_rtc_day_val, "--");
            lv_obj_center(g_lbl_rtc_day_val);

            lv_obj_t* btn_p1d = lv_btn_create(row_d);
            lv_obj_set_size(btn_p1d, 100, 42);
            lv_obj_set_style_bg_color(btn_p1d, lv_color_hex(0x1E293B), 0);
            lv_obj_set_style_border_color(btn_p1d, lv_color_hex(0x334155), 0);
            lv_obj_set_style_border_width(btn_p1d, 1, 0);
            lv_obj_set_style_radius(btn_p1d, 8, 0);
            lv_obj_t* lp1d = lv_label_create(btn_p1d);
            lv_label_set_text(lp1d, "+1 Giorno");
            lv_obj_set_style_text_font(lp1d, &lv_font_montserrat_14, 0);
            lv_obj_center(lp1d);
            lv_obj_add_event_cb(btn_p1d, [](lv_event_t*) {
                abarth::storage::rtc().adjustDays(1);
                refresh_rtc_ui();
            }, LV_EVENT_CLICKED, nullptr);

            // --- Riga Mese ---
            lv_obj_t* row_mo = lv_obj_create(c3);
            lv_obj_remove_style_all(row_mo);
            lv_obj_set_width(row_mo, LV_PCT(100));
            lv_obj_set_height(row_mo, 48);
            lv_obj_set_flex_flow(row_mo, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(row_mo, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_set_style_pad_gap(row_mo, 10, 0);

            lv_obj_t* lbl_mo_title = lv_label_create(row_mo);
            lv_obj_set_width(lbl_mo_title, 85);
            lv_label_set_text(lbl_mo_title, "Mese:");
            lv_obj_set_style_text_color(lbl_mo_title, lv_color_hex(0xFFFFFF), 0);
            lv_obj_set_style_text_font(lbl_mo_title, &lv_font_montserrat_16, 0);

            lv_obj_t* btn_m1mo = lv_btn_create(row_mo);
            lv_obj_set_size(btn_m1mo, 100, 42);
            lv_obj_set_style_bg_color(btn_m1mo, lv_color_hex(0x1E293B), 0);
            lv_obj_set_style_border_color(btn_m1mo, lv_color_hex(0x334155), 0);
            lv_obj_set_style_border_width(btn_m1mo, 1, 0);
            lv_obj_set_style_radius(btn_m1mo, 8, 0);
            lv_obj_t* lm1mo = lv_label_create(btn_m1mo);
            lv_label_set_text(lm1mo, "-1 Mese");
            lv_obj_set_style_text_font(lm1mo, &lv_font_montserrat_14, 0);
            lv_obj_center(lm1mo);
            lv_obj_add_event_cb(btn_m1mo, [](lv_event_t*) {
                abarth::storage::rtc().adjustMonths(-1);
                refresh_rtc_ui();
            }, LV_EVENT_CLICKED, nullptr);

            lv_obj_t* cont_mo_val = lv_obj_create(row_mo);
            lv_obj_set_size(cont_mo_val, 110, 42);
            lv_obj_set_style_bg_color(cont_mo_val, lv_color_hex(0x0F172A), 0);
            lv_obj_set_style_border_color(cont_mo_val, lv_color_hex(0x34D399), 0);
            lv_obj_set_style_border_width(cont_mo_val, 1, 0);
            lv_obj_set_style_radius(cont_mo_val, 8, 0);
            g_lbl_rtc_month_val = lv_label_create(cont_mo_val);
            lv_obj_set_style_text_color(g_lbl_rtc_month_val, lv_color_hex(0x34D399), 0);
            lv_obj_set_style_text_font(g_lbl_rtc_month_val, &lv_font_montserrat_16, 0);
            lv_label_set_text(g_lbl_rtc_month_val, "--");
            lv_obj_center(g_lbl_rtc_month_val);

            lv_obj_t* btn_p1mo = lv_btn_create(row_mo);
            lv_obj_set_size(btn_p1mo, 100, 42);
            lv_obj_set_style_bg_color(btn_p1mo, lv_color_hex(0x1E293B), 0);
            lv_obj_set_style_border_color(btn_p1mo, lv_color_hex(0x334155), 0);
            lv_obj_set_style_border_width(btn_p1mo, 1, 0);
            lv_obj_set_style_radius(btn_p1mo, 8, 0);
            lv_obj_t* lp1mo = lv_label_create(btn_p1mo);
            lv_label_set_text(lp1mo, "+1 Mese");
            lv_obj_set_style_text_font(lp1mo, &lv_font_montserrat_14, 0);
            lv_obj_center(lp1mo);
            lv_obj_add_event_cb(btn_p1mo, [](lv_event_t*) {
                abarth::storage::rtc().adjustMonths(1);
                refresh_rtc_ui();
            }, LV_EVENT_CLICKED, nullptr);

            // --- Riga Anno ---
            lv_obj_t* row_y = lv_obj_create(c3);
            lv_obj_remove_style_all(row_y);
            lv_obj_set_width(row_y, LV_PCT(100));
            lv_obj_set_height(row_y, 48);
            lv_obj_set_flex_flow(row_y, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(row_y, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_set_style_pad_gap(row_y, 10, 0);

            lv_obj_t* lbl_y_title = lv_label_create(row_y);
            lv_obj_set_width(lbl_y_title, 85);
            lv_label_set_text(lbl_y_title, "Anno:");
            lv_obj_set_style_text_color(lbl_y_title, lv_color_hex(0xFFFFFF), 0);
            lv_obj_set_style_text_font(lbl_y_title, &lv_font_montserrat_16, 0);

            lv_obj_t* btn_m1y = lv_btn_create(row_y);
            lv_obj_set_size(btn_m1y, 100, 42);
            lv_obj_set_style_bg_color(btn_m1y, lv_color_hex(0x1E293B), 0);
            lv_obj_set_style_border_color(btn_m1y, lv_color_hex(0x334155), 0);
            lv_obj_set_style_border_width(btn_m1y, 1, 0);
            lv_obj_set_style_radius(btn_m1y, 8, 0);
            lv_obj_t* lm1y = lv_label_create(btn_m1y);
            lv_label_set_text(lm1y, "-1 Anno");
            lv_obj_set_style_text_font(lm1y, &lv_font_montserrat_14, 0);
            lv_obj_center(lm1y);
            lv_obj_add_event_cb(btn_m1y, [](lv_event_t*) {
                abarth::storage::rtc().adjustYears(-1);
                refresh_rtc_ui();
            }, LV_EVENT_CLICKED, nullptr);

            lv_obj_t* cont_y_val = lv_obj_create(row_y);
            lv_obj_set_size(cont_y_val, 90, 42);
            lv_obj_set_style_bg_color(cont_y_val, lv_color_hex(0x0F172A), 0);
            lv_obj_set_style_border_color(cont_y_val, lv_color_hex(0x34D399), 0);
            lv_obj_set_style_border_width(cont_y_val, 1, 0);
            lv_obj_set_style_radius(cont_y_val, 8, 0);
            g_lbl_rtc_year_val = lv_label_create(cont_y_val);
            lv_obj_set_style_text_color(g_lbl_rtc_year_val, lv_color_hex(0x34D399), 0);
            lv_obj_set_style_text_font(g_lbl_rtc_year_val, &lv_font_montserrat_20, 0);
            lv_label_set_text(g_lbl_rtc_year_val, "----");
            lv_obj_center(g_lbl_rtc_year_val);

            lv_obj_t* btn_p1y = lv_btn_create(row_y);
            lv_obj_set_size(btn_p1y, 100, 42);
            lv_obj_set_style_bg_color(btn_p1y, lv_color_hex(0x1E293B), 0);
            lv_obj_set_style_border_color(btn_p1y, lv_color_hex(0x334155), 0);
            lv_obj_set_style_border_width(btn_p1y, 1, 0);
            lv_obj_set_style_radius(btn_p1y, 8, 0);
            lv_obj_t* lp1y = lv_label_create(btn_p1y);
            lv_label_set_text(lp1y, "+1 Anno");
            lv_obj_set_style_text_font(lp1y, &lv_font_montserrat_14, 0);
            lv_obj_center(lp1y);
            lv_obj_add_event_cb(btn_p1y, [](lv_event_t*) {
                abarth::storage::rtc().adjustYears(1);
                refresh_rtc_ui();
            }, LV_EVENT_CLICKED, nullptr);

            // Card 4: Sincronizzazione Automatica & Opzioni
            lv_obj_t* c4 = make_card_group(g_content_panel, "Sincronizzazione Automatica & Reset", LV_SYMBOL_WIFI);
            make_sub_label(c4, "Sincronizza l'orologio atomico via WiFi o ripristina la data del firmware:");

            lv_obj_t* r_sync_btns = lv_obj_create(c4);
            lv_obj_remove_style_all(r_sync_btns);
            lv_obj_set_width(r_sync_btns, LV_PCT(100));
            lv_obj_set_height(r_sync_btns, 46);
            lv_obj_set_flex_flow(r_sync_btns, LV_FLEX_FLOW_ROW);
            lv_obj_set_style_pad_gap(r_sync_btns, 12, 0);

            // Tasto Sync NTP
            lv_obj_t* btn_ntp = lv_btn_create(r_sync_btns);
            lv_obj_set_flex_grow(btn_ntp, 1);
            lv_obj_set_height(btn_ntp, 44);
            lv_obj_set_style_bg_color(btn_ntp, lv_color_hex(0x0284C7), 0);
            lv_obj_set_style_radius(btn_ntp, 10, 0);
            lv_obj_t* l_ntp = lv_label_create(btn_ntp);
            lv_label_set_text(l_ntp, LV_SYMBOL_WIFI " Sincronizza NTP (WiFi)");
            lv_obj_set_style_text_font(l_ntp, &lv_font_montserrat_14, 0);
            lv_obj_center(l_ntp);
            lv_obj_add_event_cb(btn_ntp, [](lv_event_t*) {
                abarth::storage::rtc().enableNtpSync();
                esp_sntp_restart();
                refresh_rtc_ui();
            }, LV_EVENT_CLICKED, nullptr);

            // Tasto Reset Build
            lv_obj_t* btn_build = lv_btn_create(r_sync_btns);
            lv_obj_set_flex_grow(btn_build, 1);
            lv_obj_set_height(btn_build, 44);
            lv_obj_set_style_bg_color(btn_build, lv_color_hex(0x7C3AED), 0);
            lv_obj_set_style_radius(btn_build, 10, 0);
            lv_obj_t* l_build = lv_label_create(btn_build);
            lv_label_set_text(l_build, LV_SYMBOL_REFRESH " Reset Data/Ora Build");
            lv_obj_set_style_text_font(l_build, &lv_font_montserrat_14, 0);
            lv_obj_center(l_build);
            lv_obj_add_event_cb(btn_build, [](lv_event_t*) {
                abarth::storage::rtc().setTimeFromCompile();
                refresh_rtc_ui();
            }, LV_EVENT_CLICKED, nullptr);

            // Popola subito i valori correnti
            refresh_rtc_ui();
            break;
        }

        // === 10: MEMORIA MICROSD ===
        case 10: {
            auto& sd_mgr = abarth::storage::sd();
            const bool mounted = sd_mgr.isMounted();

            // Card 1: Stato & Capacità Archiviazione
            lv_obj_t* c1 = make_card_group(g_content_panel, "Stato & Capacita' Scheda MicroSD", LV_SYMBOL_SD_CARD);

            // Riga di stato montaggio in evidenza
            lv_obj_t* row_st = lv_obj_create(c1);
            lv_obj_remove_style_all(row_st);
            lv_obj_set_width(row_st, LV_PCT(100));
            lv_obj_set_height(row_st, 42);
            lv_obj_set_style_bg_color(row_st, lv_color_hex(0x181826), 0);
            lv_obj_set_style_bg_opa(row_st, LV_OPA_COVER, 0);
            lv_obj_set_style_border_color(row_st, lv_color_hex(0x28283C), 0);
            lv_obj_set_style_border_width(row_st, 1, 0);
            lv_obj_set_style_radius(row_st, 8, 0);
            lv_obj_set_style_pad_hor(row_st, 14, 0);
            lv_obj_set_flex_flow(row_st, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(row_st, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

            lv_obj_t* lbl_st_t = lv_label_create(row_st);
            lv_label_set_text(lbl_st_t, "Stato Filesystem FatFS:");
            lv_obj_set_style_text_color(lbl_st_t, lv_color_hex(0xFFFFFF), 0);
            lv_obj_set_style_text_font(lbl_st_t, &lv_font_montserrat_14, 0);

            lv_obj_t* badge_sd = lv_obj_create(row_st);
            lv_obj_remove_style_all(badge_sd);
            lv_obj_set_size(badge_sd, LV_SIZE_CONTENT, 30);
            lv_obj_set_style_radius(badge_sd, 6, 0);
            lv_obj_set_style_pad_hor(badge_sd, 12, 0);
            lv_obj_set_style_pad_ver(badge_sd, 4, 0);
            lv_obj_set_style_bg_color(badge_sd, lv_color_hex(mounted ? 0x064E3B : 0x450A0A), 0);
            lv_obj_set_style_border_color(badge_sd, lv_color_hex(mounted ? 0x10B981 : 0xEF4444), 0);
            lv_obj_set_style_border_width(badge_sd, 1, 0);

            lv_obj_t* lbl_badge_sd = lv_label_create(badge_sd);
            lv_label_set_text(lbl_badge_sd, mounted ? LV_SYMBOL_OK " MONTATA (OK)" : LV_SYMBOL_WARNING " NON MONTATA");
            lv_obj_set_style_text_color(lbl_badge_sd, lv_color_hex(mounted ? 0x34D399 : 0xF87171), 0);
            lv_obj_set_style_text_font(lbl_badge_sd, &lv_font_montserrat_12, 0);

            if (mounted) {
                const uint64_t total_mb = sd_mgr.getTotalSizeMB();
                const uint64_t free_mb  = sd_mgr.getFreeSizeMB();
                const uint64_t used_mb  = sd_mgr.getUsedSizeMB();
                const int pct_used      = (total_mb > 0) ? static_cast<int>((used_mb * 100) / total_mb) : 0;

                // Barra grafica di riempimento
                lv_obj_t* bar_sd = lv_bar_create(c1);
                lv_obj_set_width(bar_sd, LV_PCT(100));
                lv_obj_set_height(bar_sd, 16);
                lv_bar_set_range(bar_sd, 0, 100);
                lv_bar_set_value(bar_sd, pct_used, LV_ANIM_OFF);
                lv_obj_set_style_bg_color(bar_sd, lv_color_hex(0x1E293B), LV_PART_MAIN);
                lv_obj_set_style_radius(bar_sd, 6, LV_PART_MAIN);
                lv_obj_set_style_bg_color(bar_sd, lv_color_hex(0x10B981), LV_PART_INDICATOR);
                lv_obj_set_style_radius(bar_sd, 6, LV_PART_INDICATOR);

                // Riga 3 box con metriche di capacità
                lv_obj_t* row_boxes = lv_obj_create(c1);
                lv_obj_remove_style_all(row_boxes);
                lv_obj_set_width(row_boxes, LV_PCT(100));
                lv_obj_set_height(row_boxes, LV_SIZE_CONTENT);
                lv_obj_set_flex_flow(row_boxes, LV_FLEX_FLOW_ROW);
                lv_obj_set_style_pad_gap(row_boxes, 10, 0);

                auto make_stat_box = [&](lv_obj_t* parent, const char* label, const char* val, uint32_t val_col) {
                    lv_obj_t* box = lv_obj_create(parent);
                    lv_obj_remove_style_all(box);
                    lv_obj_set_flex_grow(box, 1);
                    lv_obj_set_height(box, 52);
                    lv_obj_set_style_bg_color(box, lv_color_hex(0x141420), 0);
                    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
                    lv_obj_set_style_border_color(box, lv_color_hex(0x28283C), 0);
                    lv_obj_set_style_border_width(box, 1, 0);
                    lv_obj_set_style_radius(box, 8, 0);
                    lv_obj_set_style_pad_all(box, 6, 0);
                    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
                    lv_obj_set_flex_align(box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

                    lv_obj_t* l_lbl = lv_label_create(box);
                    lv_label_set_text(l_lbl, label);
                    lv_obj_set_style_text_color(l_lbl, lv_color_hex(0x94A3B8), 0);
                    lv_obj_set_style_text_font(l_lbl, &lv_font_montserrat_12, 0);

                    lv_obj_t* l_val = lv_label_create(box);
                    lv_label_set_text(l_val, val);
                    lv_obj_set_style_text_color(l_val, lv_color_hex(val_col), 0);
                    lv_obj_set_style_text_font(l_val, &lv_font_montserrat_14, 0);
                };

                char buf_tot[32], buf_free[32], buf_used[32];
                std::snprintf(buf_tot, sizeof(buf_tot), "%.1f GB", total_mb / 1024.0f);
                std::snprintf(buf_free, sizeof(buf_free), "%.1f GB", free_mb / 1024.0f);
                std::snprintf(buf_used, sizeof(buf_used), "%llu MB (%d%%)", (unsigned long long)used_mb, pct_used);

                make_stat_box(row_boxes, "Capacita' Totale", buf_tot, 0x38BDF8);
                make_stat_box(row_boxes, "Spazio Libero", buf_free, 0x34D399);
                make_stat_box(row_boxes, "Spazio Occupato", buf_used, 0xF59E0B);

                // Dettagli bus hardware
                lv_obj_t* lbl_bus = lv_label_create(c1);
                lv_obj_set_style_text_color(lbl_bus, lv_color_hex(0x94A3B8), 0);
                lv_obj_set_style_text_font(lbl_bus, &lv_font_montserrat_12, 0);
                lv_label_set_long_mode(lbl_bus, LV_LABEL_LONG_WRAP);
                lv_obj_set_width(lbl_bus, LV_PCT(100));
                lv_label_set_text(lbl_bus,
                    "• Controller Bus: ESP32-P4 SDMMC Slot 0 a 4-bit (40 MHz)\n"
                    "• Pinout Hardware: D0=GPIO39, D1=GPIO40, D2=GPIO41, D3=GPIO42, CLK=GPIO43, CMD=GPIO44\n"
                    "• Filesystem & Montaggio: FatFS montato su /sdcard | LVGL FileSystem su drive 'S:/'");
            } else {
                lv_obj_t* lbl_nosd = lv_label_create(c1);
                lv_obj_set_style_text_color(lbl_nosd, lv_color_hex(0xFCA5A5), 0);
                lv_obj_set_style_text_font(lbl_nosd, &lv_font_montserrat_12, 0);
                lv_label_set_long_mode(lbl_nosd, LV_LABEL_LONG_WRAP);
                lv_obj_set_width(lbl_nosd, LV_PCT(100));
                lv_label_set_text(lbl_nosd,
                    "Nessuna scheda MicroSD rilevata o inizializzata.\n"
                    "Inserire una scheda MicroSD formattata in FAT32 nello slot laterale e premere il pulsante sotto per rimontarla.");

                lv_obj_t* btn_remount_nosd = lv_btn_create(c1);
                lv_obj_set_width(btn_remount_nosd, LV_PCT(100));
                lv_obj_set_height(btn_remount_nosd, 44);
                lv_obj_set_style_bg_color(btn_remount_nosd, lv_color_hex(0x059669), 0);
                lv_obj_set_style_radius(btn_remount_nosd, 10, 0);
                lv_obj_t* l_rm = lv_label_create(btn_remount_nosd);
                lv_label_set_text(l_rm, LV_SYMBOL_REFRESH " Inizializza / Monta Scheda MicroSD");
                lv_obj_set_style_text_font(l_rm, &lv_font_montserrat_14, 0);
                lv_obj_center(l_rm);
                lv_obj_add_event_cb(btn_remount_nosd, [](lv_event_t*) {
                    abarth::storage::sd().remount();
                    render_category(g_current_cat, 0);
                }, LV_EVENT_CLICKED, nullptr);
            }

            // Card 2: Logging Telemetria & Registrazioni Corse
            lv_obj_t* c2 = make_card_group(g_content_panel, "Registrazione Telemetria (/sdcard/logs)", LV_SYMBOL_LIST);
            make_sub_label(c2, "Salvataggio continuo di giri motore, pressione turbo, temperature e accelerazioni in formato CSV:");

            if (mounted) {
                const size_t log_count = sd_mgr.getLogFileCount();
                const uint64_t log_bytes = sd_mgr.getLogFilesTotalBytes();
                const char* cur_log = sd_mgr.getActiveLogPath();

                lv_obj_t* lbl_log_info = lv_label_create(c2);
                lv_obj_set_style_text_color(lbl_log_info, lv_color_hex(0xE2E8F0), 0);
                lv_obj_set_style_text_font(lbl_log_info, &lv_font_montserrat_12, 0);
                lv_label_set_long_mode(lbl_log_info, LV_LABEL_LONG_WRAP);
                lv_obj_set_width(lbl_log_info, LV_PCT(100));
                char lbuf[256];
                std::snprintf(lbuf, sizeof(lbuf),
                    "• File Log Attivo: %s\n"
                    "• Sessioni Archiviate: %u file CSV\n"
                    "• Spazio Occupato dai Log: %.2f MB (%llu byte)",
                    (cur_log && cur_log[0]) ? cur_log : "Nessuno (in attesa di avvio sessione)",
                    static_cast<unsigned>(log_count),
                    log_bytes / (1024.0f * 1024.0f),
                    (unsigned long long)log_bytes
                );
                lv_label_set_text(lbl_log_info, lbuf);

                // Riga Pulsanti Log
                lv_obj_t* row_log_btns = lv_obj_create(c2);
                lv_obj_remove_style_all(row_log_btns);
                lv_obj_set_width(row_log_btns, LV_PCT(100));
                lv_obj_set_height(row_log_btns, 46);
                lv_obj_set_flex_flow(row_log_btns, LV_FLEX_FLOW_ROW);
                lv_obj_set_style_pad_gap(row_log_btns, 12, 0);

                // Tasto Nuova Sessione Log
                lv_obj_t* btn_new_log = lv_btn_create(row_log_btns);
                lv_obj_set_flex_grow(btn_new_log, 1);
                lv_obj_set_height(btn_new_log, 44);
                lv_obj_set_style_bg_color(btn_new_log, lv_color_hex(0x0284C7), 0);
                lv_obj_set_style_radius(btn_new_log, 10, 0);
                lv_obj_t* l_nl = lv_label_create(btn_new_log);
                lv_label_set_text(l_nl, LV_SYMBOL_PLUS " Nuova Sessione Log");
                lv_obj_set_style_text_font(l_nl, &lv_font_montserrat_14, 0);
                lv_obj_center(l_nl);
                lv_obj_add_event_cb(btn_new_log, [](lv_event_t*) {
                    abarth::storage::sd().startNewLogSession();
                    render_category(g_current_cat, 0);
                }, LV_EVENT_CLICKED, nullptr);

                // Tasto Svuota Log
                lv_obj_t* btn_clear_log = lv_btn_create(row_log_btns);
                lv_obj_set_flex_grow(btn_clear_log, 1);
                lv_obj_set_height(btn_clear_log, 44);
                lv_obj_set_style_bg_color(btn_clear_log, lv_color_hex(0x991B1B), 0);
                lv_obj_set_style_radius(btn_clear_log, 10, 0);
                lv_obj_t* l_cl = lv_label_create(btn_clear_log);
                lv_label_set_text(l_cl, LV_SYMBOL_TRASH " Svuota Vecchi Log");
                lv_obj_set_style_text_font(l_cl, &lv_font_montserrat_14, 0);
                lv_obj_center(l_cl);
                lv_obj_add_event_cb(btn_clear_log, [](lv_event_t*) {
                    close_modal(nullptr);
                    lv_obj_t* modal = create_modal("Elimina File di Log");
                    lv_obj_t* lbl_m = lv_label_create(modal);
                    lv_label_set_text(lbl_m, "Sei sicuro di voler eliminare tutti i vecchi file di log CSV presenti sulla MicroSD?\nL'operazione e' irreversibile.");
                    lv_obj_set_style_text_color(lbl_m, lv_color_hex(0xFECACA), 0);
                    lv_obj_set_style_text_font(lbl_m, &lv_font_montserrat_14, 0);
                    lv_label_set_long_mode(lbl_m, LV_LABEL_LONG_WRAP);
                    lv_obj_set_width(lbl_m, LV_PCT(100));

                    lv_obj_t* r_mbtns = lv_obj_create(modal);
                    lv_obj_remove_style_all(r_mbtns);
                    lv_obj_set_width(r_mbtns, LV_PCT(100));
                    lv_obj_set_height(r_mbtns, 44);
                    lv_obj_set_flex_flow(r_mbtns, LV_FLEX_FLOW_ROW);
                    lv_obj_set_style_pad_gap(r_mbtns, 12, 0);

                    lv_obj_t* b_conf = lv_btn_create(r_mbtns);
                    lv_obj_set_flex_grow(b_conf, 1);
                    lv_obj_set_height(b_conf, 44);
                    lv_obj_set_style_bg_color(b_conf, lv_color_hex(0xDC2626), 0);
                    lv_obj_set_style_radius(b_conf, 8, 0);
                    lv_obj_t* lc = lv_label_create(b_conf);
                    lv_label_set_text(lc, "Conferma Eliminazione");
                    lv_obj_center(lc);
                    lv_obj_add_event_cb(b_conf, [](lv_event_t*) {
                        abarth::storage::sd().clearLogs();
                        close_modal(nullptr);
                        render_category(g_current_cat, 0);
                    }, LV_EVENT_CLICKED, nullptr);

                    lv_obj_t* b_canc = lv_btn_create(r_mbtns);
                    lv_obj_set_flex_grow(b_canc, 1);
                    lv_obj_set_height(b_canc, 44);
                    lv_obj_set_style_bg_color(b_canc, lv_color_hex(0x334155), 0);
                    lv_obj_set_style_radius(b_canc, 8, 0);
                    lv_obj_t* lcan = lv_label_create(b_canc);
                    lv_label_set_text(lcan, "Annulla");
                    lv_obj_center(lcan);
                    lv_obj_add_event_cb(b_canc, close_modal, LV_EVENT_CLICKED, nullptr);
                }, LV_EVENT_CLICKED, nullptr);
            } else {
                lv_obj_t* l_no = lv_label_create(c2);
                lv_label_set_text(l_no, "Inserire e montare la MicroSD per abilitare la telemetria continua.");
                lv_obj_set_style_text_color(l_no, lv_color_hex(0x94A3B8), 0);
                lv_obj_set_style_text_font(l_no, &lv_font_montserrat_12, 0);
            }

            // Card 3: Asset Grafici & Personalizzazioni (S:/icons/)
            lv_obj_t* c3 = make_card_group(g_content_panel, "Asset Grafici & Icone Custom (S:/icons/)", LV_SYMBOL_IMAGE);
            make_sub_label(c3, "Caricamento automatico di icone e loghi personalizzati da MicroSD al posto di quelli integrati:");

            if (mounted) {
                const size_t icons_n = sd_mgr.getIconsCount();
                char ibuf[64];
                std::snprintf(ibuf, sizeof(ibuf), "• File PNG trovati nella cartella /sdcard/icons: %u", static_cast<unsigned>(icons_n));
                lv_obj_t* l_ic = lv_label_create(c3);
                lv_label_set_text(l_ic, ibuf);
                lv_obj_set_style_text_color(l_ic, lv_color_hex(0x38BDF8), 0);
                lv_obj_set_style_text_font(l_ic, &lv_font_montserrat_12, 0);

                // Helper pill per file noti
                auto make_asset_pill = [&](lv_obj_t* parent, const char* fname, const char* desc) {
                    lv_obj_t* r = lv_obj_create(parent);
                    lv_obj_remove_style_all(r);
                    lv_obj_set_width(r, LV_PCT(100));
                    lv_obj_set_height(r, 38);
                    lv_obj_set_style_bg_color(r, lv_color_hex(0x181826), 0);
                    lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
                    lv_obj_set_style_border_color(r, lv_color_hex(0x28283C), 0);
                    lv_obj_set_style_border_width(r, 1, 0);
                    lv_obj_set_style_radius(r, 6, 0);
                    lv_obj_set_style_pad_hor(r, 12, 0);
                    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
                    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

                    lv_obj_t* lt = lv_label_create(r);
                    char tbuf[64];
                    std::snprintf(tbuf, sizeof(tbuf), "%s (%s)", fname, desc);
                    lv_label_set_text(lt, tbuf);
                    lv_obj_set_style_text_color(lt, lv_color_hex(0xFFFFFF), 0);
                    lv_obj_set_style_text_font(lt, &lv_font_montserrat_12, 0);

                    char full_p[128];
                    std::snprintf(full_p, sizeof(full_p), "/sdcard/icons/%s", fname);
                    FILE* f = fopen(full_p, "rb");
                    bool exists = (f != nullptr);
                    if (f) fclose(f);

                    lv_obj_t* st_lbl = lv_label_create(r);
                    lv_label_set_text(st_lbl, exists ? LV_SYMBOL_OK " Su MicroSD" : "Predefinito");
                    lv_obj_set_style_text_color(st_lbl, lv_color_hex(exists ? 0x34D399 : 0x888899), 0);
                    lv_obj_set_style_text_font(st_lbl, &lv_font_montserrat_12, 0);
                };

                make_asset_pill(c3, "abarth_logo.png", "Logo Abarth Manometri");
                make_asset_pill(c3, "abarth_splash.png", "Logo Splashscreen Avvio");
                make_asset_pill(c3, "scorpion.png", "Filigrana Scorpione Sfondo");
                make_asset_pill(c3, "oil_blue.png", "Icona Olio Virtuale");
            } else {
                lv_obj_t* l_no = lv_label_create(c3);
                lv_label_set_text(l_no, "Vengono utilizzati gli asset predefiniti compilati nella memoria Flash.");
                lv_obj_set_style_text_color(l_no, lv_color_hex(0x94A3B8), 0);
                lv_obj_set_style_text_font(l_no, &lv_font_montserrat_12, 0);
            }

            // Card 4: Diagnostica & Benchmark Velocità
            lv_obj_t* c4 = make_card_group(g_content_panel, "Diagnostica Hardware & Benchmark", LV_SYMBOL_SETTINGS);
            make_sub_label(c4, "Misurazione reale del throughput di lettura e scrittura su bus SDMMC 4-bit (512 KB):");

            lv_obj_t* bench_res_box = lv_obj_create(c4);
            lv_obj_remove_style_all(bench_res_box);
            lv_obj_set_width(bench_res_box, LV_PCT(100));
            lv_obj_set_height(bench_res_box, 44);
            lv_obj_set_style_bg_color(bench_res_box, lv_color_hex(0x181826), 0);
            lv_obj_set_style_bg_opa(bench_res_box, LV_OPA_COVER, 0);
            lv_obj_set_style_border_color(bench_res_box, lv_color_hex(0x28283C), 0);
            lv_obj_set_style_border_width(bench_res_box, 1, 0);
            lv_obj_set_style_radius(bench_res_box, 8, 0);
            lv_obj_set_style_pad_hor(bench_res_box, 14, 0);
            lv_obj_set_flex_flow(bench_res_box, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(bench_res_box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

            lv_obj_t* lbl_bench = lv_label_create(bench_res_box);
            lv_label_set_text(lbl_bench, "Velocita' Scrittura: -- MB/s  |  Velocita' Lettura: -- MB/s");
            lv_obj_set_style_text_color(lbl_bench, lv_color_hex(0x38BDF8), 0);
            lv_obj_set_style_text_font(lbl_bench, &lv_font_montserrat_14, 0);

            // Riga pulsanti benchmark e rimonta
            lv_obj_t* row_b_btns = lv_obj_create(c4);
            lv_obj_remove_style_all(row_b_btns);
            lv_obj_set_width(row_b_btns, LV_PCT(100));
            lv_obj_set_height(row_b_btns, 46);
            lv_obj_set_flex_flow(row_b_btns, LV_FLEX_FLOW_ROW);
            lv_obj_set_style_pad_gap(row_b_btns, 12, 0);

            lv_obj_t* btn_bench = lv_btn_create(row_b_btns);
            lv_obj_set_flex_grow(btn_bench, 1);
            lv_obj_set_height(btn_bench, 44);
            lv_obj_set_style_bg_color(btn_bench, lv_color_hex(0x0284C7), 0);
            lv_obj_set_style_radius(btn_bench, 10, 0);
            lv_obj_t* l_bb = lv_label_create(btn_bench);
            lv_label_set_text(l_bb, LV_SYMBOL_PLAY " Esegui Test Velocita'");
            lv_obj_set_style_text_font(l_bb, &lv_font_montserrat_14, 0);
            lv_obj_center(l_bb);

            lv_obj_add_event_cb(btn_bench, [](lv_event_t* e) {
                lv_obj_t* target_lbl = static_cast<lv_obj_t*>(lv_event_get_user_data(e));
                lv_label_set_text(target_lbl, "Esecuzione benchmark in corso (512 KB)...");
                lv_refr_now(nullptr);

                float w_speed = 0.0f, r_speed = 0.0f;
                bool ok = abarth::storage::sd().runBenchmark(&w_speed, &r_speed);
                if (ok) {
                    char res_buf[96];
                    std::snprintf(res_buf, sizeof(res_buf),
                        LV_SYMBOL_OK " Scrittura: %.1f MB/s  |  Lettura: %.1f MB/s",
                        w_speed, r_speed);
                    lv_label_set_text(target_lbl, res_buf);
                    lv_obj_set_style_text_color(target_lbl, lv_color_hex(0x22C55E), 0);
                } else {
                    lv_label_set_text(target_lbl, LV_SYMBOL_WARNING " Errore durante il benchmark!");
                    lv_obj_set_style_text_color(target_lbl, lv_color_hex(0xEF4444), 0);
                }
            }, LV_EVENT_CLICKED, lbl_bench);

            lv_obj_t* btn_remount = lv_btn_create(row_b_btns);
            lv_obj_set_flex_grow(btn_remount, 1);
            lv_obj_set_height(btn_remount, 44);
            lv_obj_set_style_bg_color(btn_remount, lv_color_hex(0x7C3AED), 0);
            lv_obj_set_style_radius(btn_remount, 10, 0);
            lv_obj_t* l_rm = lv_label_create(btn_remount);
            lv_label_set_text(l_rm, LV_SYMBOL_REFRESH " Rimonta / Reinizializza");
            lv_obj_set_style_text_font(l_rm, &lv_font_montserrat_14, 0);
            lv_obj_center(l_rm);
            lv_obj_add_event_cb(btn_remount, [](lv_event_t*) {
                abarth::storage::sd().remount();
                render_category(g_current_cat, 0);
            }, LV_EVENT_CLICKED, nullptr);

            // Card 5: Centro Controllo Web & Download Telemetria
            lv_obj_t* c5 = make_card_group(g_content_panel, "Centro Controllo Web & Download Telemetria", LV_SYMBOL_WIFI);
            make_sub_label(c5, "Accedi alla dashboard wireless da smartphone o PC per scaricare i file di telemetria CSV e aggiornare il firmware OTA:");

            lv_obj_t* web_box = lv_obj_create(c5);
            lv_obj_remove_style_all(web_box);
            lv_obj_set_width(web_box, LV_PCT(100));
            lv_obj_set_height(web_box, LV_SIZE_CONTENT);
            lv_obj_set_style_bg_color(web_box, lv_color_hex(0x181826), 0);
            lv_obj_set_style_bg_opa(web_box, LV_OPA_COVER, 0);
            lv_obj_set_style_border_color(web_box, lv_color_hex(0x28283C), 0);
            lv_obj_set_style_border_width(web_box, 1, 0);
            lv_obj_set_style_radius(web_box, 8, 0);
            lv_obj_set_style_pad_all(web_box, 12, 0);
            lv_obj_set_flex_flow(web_box, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_style_pad_gap(web_box, 6, 0);

            const bool web_active = abarth::web::isRunning();
            std::string web_url = abarth::web::getServerUrl();

            lv_obj_t* lbl_w_status = lv_label_create(web_box);
            char w_stat_buf[128];
            std::snprintf(w_stat_buf, sizeof(w_stat_buf),
                "Stato Server HTTP: %s",
                web_active ? "#22C55E ATTIVO (Porta 80, si spegne dopo 10 min)#" : "#EF4444 DISATTIVO#");
            lv_label_set_recolor(lbl_w_status, true);
            lv_label_set_text(lbl_w_status, w_stat_buf);
            lv_obj_set_style_text_font(lbl_w_status, &lv_font_montserrat_14, 0);

            lv_obj_t* lbl_w_url = lv_label_create(web_box);
            char w_url_buf[160];
            std::snprintf(w_url_buf, sizeof(w_url_buf),
                "Indirizzo Browser: #38BDF8 %s#", web_url.c_str());
            lv_label_set_recolor(lbl_w_url, true);
            lv_label_set_text(lbl_w_url, w_url_buf);
            lv_obj_set_style_text_font(lbl_w_url, &lv_font_montserrat_14, 0);

            lv_obj_t* btn_toggle_web = lv_btn_create(c5);
            lv_obj_set_width(btn_toggle_web, LV_PCT(100));
            lv_obj_set_height(btn_toggle_web, 44);
            lv_obj_set_style_bg_color(btn_toggle_web, lv_color_hex(web_active ? 0xDC2626 : 0x059669), 0);
            lv_obj_set_style_radius(btn_toggle_web, 10, 0);
            lv_obj_t* l_tw = lv_label_create(btn_toggle_web);
            lv_label_set_text(l_tw, web_active ? LV_SYMBOL_POWER " Arresta Web Server" : LV_SYMBOL_WIFI " Avvia Web Server HTTP");
            lv_obj_set_style_text_font(l_tw, &lv_font_montserrat_14, 0);
            lv_obj_center(l_tw);

            lv_obj_add_event_cb(btn_toggle_web, [](lv_event_t*) {
                if (abarth::web::isRunning()) {
                    abarth::web::stopServer();
                } else {
                    abarth::web::startServer();
                }
                render_category(g_current_cat, 0);
            }, LV_EVENT_CLICKED, nullptr);

            break;
        }

        // === 11: AVVISI & ALERT TELEMATICI ===
        case 11: {
            const bool is_portrait = (LV_HOR_RES < LV_VER_RES);

            // Segmented Top Navigation Bar per Sottomenu Avvisi
            lv_obj_t* subnav = lv_obj_create(g_content_panel);
            lv_obj_remove_style_all(subnav);
            lv_obj_set_width(subnav, LV_PCT(100));
            lv_obj_set_height(subnav, 40);
            lv_obj_set_flex_flow(subnav, LV_FLEX_FLOW_ROW);
            lv_obj_set_style_pad_gap(subnav, 8, 0);

            struct SubTabDef { const char* icon; const char* name; };
            const SubTabDef subtabs[3] = {
                {LV_SYMBOL_BELL,     is_portrait ? "Base" : "Allarmi Base"},
                {LV_SYMBOL_EDIT,     is_portrait ? "Custom" : "Personalizzati"},
                {LV_SYMBOL_REFRESH,  is_portrait ? "Warmup" : "Warmup Olio"}
            };

            for (uint16_t si = 0; si < 3; ++si) {
                lv_obj_t* sbtn = lv_btn_create(subnav);
                lv_obj_set_flex_grow(sbtn, 1);
                lv_obj_set_height(sbtn, 38);
                lv_obj_set_style_radius(sbtn, 8, 0);
                if (si == subtab_idx) {
                    lv_obj_set_style_bg_color(sbtn, lv_color_hex(0xCC3030), 0);
                    lv_obj_set_style_border_width(sbtn, 0, 0);
                } else {
                    lv_obj_set_style_bg_color(sbtn, lv_color_hex(0x181824), 0);
                    lv_obj_set_style_border_color(sbtn, lv_color_hex(0x28283C), 0);
                    lv_obj_set_style_border_width(sbtn, 1, 0);
                }
                lv_obj_t* slbl = lv_label_create(sbtn);
                char s_buf[48];
                std::snprintf(s_buf, sizeof(s_buf), "%s %s", subtabs[si].icon, subtabs[si].name);
                lv_label_set_text(slbl, s_buf);
                lv_obj_set_style_text_color(slbl, lv_color_hex(si == subtab_idx ? 0xFFFFFF : 0xAAAAAA), 0);
                lv_obj_set_style_text_font(slbl, &lv_font_montserrat_14, 0);
                lv_obj_center(slbl);

                lv_obj_add_event_cb(sbtn, [](lv_event_t* e) {
                    uint16_t target = (uint16_t)(uintptr_t)lv_event_get_user_data(e);
                    render_category(g_current_cat, target);
                }, LV_EVENT_CLICKED, (void*)(uintptr_t)si);
            }

            // Contenitore compatto del sottomenu attivo (lo scroll e' gestito dal parent g_content_panel)
            lv_obj_t* sub_content = lv_obj_create(g_content_panel);
            lv_obj_remove_style_all(sub_content);
            lv_obj_set_width(sub_content, LV_PCT(100));
            lv_obj_set_height(sub_content, LV_SIZE_CONTENT);
            lv_obj_set_flex_flow(sub_content, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_style_pad_row(sub_content, 8, 0);
            lv_obj_clear_flag(sub_content, LV_OBJ_FLAG_SCROLLABLE);

            if (subtab_idx == 0) {
                // ==================== SUBMENU 0: ALLARMI BASE ====================
                struct ValueEditData {
                    abarth::config::AlertType type;
                    bool is_threshold2;
                    lv_obj_t* value_btn_lbl;
                };

                auto open_num_keypad = [](lv_event_t* e) {
                    auto* data = static_cast<ValueEditData*>(lv_event_get_user_data(e));
                    auto* rule = abarth::config::alerts().getRule(data->type);
                    if (!rule) return;

                    float current_val = data->is_threshold2 ? rule->threshold2 : rule->threshold1;
                    close_modal(nullptr);
                    lv_obj_t* modal = create_modal("Imposta Valore Soglia");

                    lv_obj_t* lbl_info = lv_label_create(modal);
                    lv_label_set_text_fmt(lbl_info, "Nuovo valore per: %s", rule->name.c_str());
                    lv_obj_set_style_text_color(lbl_info, lv_color_hex(0xFFFFFF), 0);
                    lv_obj_set_style_text_font(lbl_info, &lv_font_montserrat_14, 0);

                    lv_obj_t* ta = lv_textarea_create(modal);
                    lv_obj_set_width(ta, LV_PCT(100));
                    lv_textarea_set_one_line(ta, true);
                    char init_buf[32];
                    if (data->type == abarth::config::AlertType::HighBoostPressure) std::snprintf(init_buf, sizeof(init_buf), "%.2f", current_val);
                    else if (data->type == abarth::config::AlertType::LowBatteryVoltage) std::snprintf(init_buf, sizeof(init_buf), "%.1f", current_val);
                    else std::snprintf(init_buf, sizeof(init_buf), "%.0f", current_val);
                    lv_textarea_set_text(ta, init_buf);
                    lv_obj_set_style_text_font(ta, &lv_font_montserrat_20, 0);
                    lv_obj_set_style_text_color(ta, lv_color_hex(0x00FFCC), 0);

                    lv_obj_t* kb = lv_keyboard_create(modal);
                    lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_NUMBER);
                    lv_keyboard_set_textarea(kb, ta);
                    lv_obj_set_size(kb, LV_PCT(100), LV_PCT(60));

                    struct SaveValueContext {
                        abarth::config::AlertType type;
                        bool is_threshold2;
                        lv_obj_t* ta;
                        lv_obj_t* target_lbl;
                    };
                    SaveValueContext* ctx = new SaveValueContext{data->type, data->is_threshold2, ta, data->value_btn_lbl};

                    lv_obj_t* btn_save = lv_btn_create(modal);
                    lv_obj_set_width(btn_save, LV_PCT(100));
                    lv_obj_set_height(btn_save, 44);
                    lv_obj_set_style_bg_color(btn_save, lv_color_hex(0xCC3030), 0);
                    lv_obj_set_style_radius(btn_save, 10, 0);
                    lv_obj_t* l_save = lv_label_create(btn_save);
                    lv_label_set_text(l_save, "Salva Valore");
                    lv_obj_set_style_text_color(l_save, lv_color_hex(0xFFFFFF), 0);
                    lv_obj_set_style_text_font(l_save, &lv_font_montserrat_16, 0);
                    lv_obj_center(l_save);

                    lv_obj_add_event_cb(btn_save, [](lv_event_t* ev) {
                        auto* c = static_cast<SaveValueContext*>(lv_event_get_user_data(ev));
                        float new_val = std::atof(lv_textarea_get_text(c->ta));
                        if (new_val < 0.0f) new_val = 0.0f;
                        if (c->is_threshold2) abarth::config::alerts().setRuleThreshold2(c->type, new_val);
                        else abarth::config::alerts().setRuleThreshold1(c->type, new_val);

                        char buf[48];
                        if (c->type == abarth::config::AlertType::HighBoostPressure) std::snprintf(buf, sizeof(buf), "Soglia: %.2f bar", new_val);
                        else if (c->type == abarth::config::AlertType::LowBatteryVoltage) std::snprintf(buf, sizeof(buf), "Soglia: %.1f V", new_val);
                        else if (c->type == abarth::config::AlertType::ColdEngineRpm) {
                            if (c->is_threshold2) std::snprintf(buf, sizeof(buf), "Sotto: %.0f \xC2\xB0""C", new_val);
                            else std::snprintf(buf, sizeof(buf), "Giri: %.0f RPM", new_val);
                        } else std::snprintf(buf, sizeof(buf), "Soglia: %.0f \xC2\xB0""C", new_val);
                        lv_label_set_text(c->target_lbl, buf);
                        delete c;
                        close_modal(nullptr);
                    }, LV_EVENT_CLICKED, ctx);
                };

                auto rules = abarth::config::alerts().rules();
                for (const auto& r : rules) {
                    abarth::config::AlertType type = r.type;
                    lv_obj_t* card = make_card_group(sub_content, r.name.c_str(), LV_SYMBOL_WARNING);
                    lv_obj_set_style_pad_all(card, 12, 0);
                    lv_obj_set_style_pad_row(card, 8, 0);

                    // Switch di stato integrato direttamente nell'header a destra
                    lv_obj_t* h_row = lv_obj_get_child(card, 0);
                    lv_obj_t* sw = lv_switch_create(h_row);
                    lv_obj_set_style_bg_color(sw, lv_color_hex(0x00CC66), (lv_style_selector_t)((uint32_t)LV_PART_INDICATOR | (uint32_t)LV_STATE_CHECKED));
                    if (r.enabled) lv_obj_add_state(sw, LV_STATE_CHECKED);
                    lv_obj_add_event_cb(sw, [](lv_event_t* e) {
                        abarth::config::AlertType t = (abarth::config::AlertType)(uintptr_t)lv_event_get_user_data(e);
                        bool en = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
                        abarth::config::alerts().setRuleEnabled(t, en);
                    }, LV_EVENT_VALUE_CHANGED, (void*)(uintptr_t)type);

                    // Descrizione breve
                    lv_obj_t* desc_lbl = lv_label_create(card);
                    lv_label_set_text(desc_lbl, r.description.c_str());
                    lv_obj_set_style_text_color(desc_lbl, lv_color_hex(0x8E8E9F), 0);
                    lv_obj_set_style_text_font(desc_lbl, &lv_font_montserrat_12, 0);
                    lv_label_set_long_mode(desc_lbl, LV_LABEL_LONG_WRAP);
                    lv_obj_set_width(desc_lbl, LV_PCT(100));

                    // Riga compatta controlli: riempie il 100% della card senza vuoti
                    lv_obj_t* row_c = lv_obj_create(card);
                    lv_obj_remove_style_all(row_c);
                    lv_obj_set_width(row_c, LV_PCT(100));
                    lv_obj_set_height(row_c, 38);
                    lv_obj_set_flex_flow(row_c, LV_FLEX_FLOW_ROW);
                    lv_obj_set_flex_align(row_c, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
                    lv_obj_set_style_pad_gap(row_c, 8, 0);

                    if (type == abarth::config::AlertType::ColdEngineRpm) {
                        // Pulsante Giri Max
                        lv_obj_t* btn_v1 = lv_btn_create(row_c);
                        lv_obj_set_flex_grow(btn_v1, 3);
                        lv_obj_set_height(btn_v1, 38);
                        lv_obj_set_style_bg_color(btn_v1, lv_color_hex(0x1C1C2A), 0);
                        lv_obj_set_style_border_color(btn_v1, lv_color_hex(0xCC3030), 0);
                        lv_obj_set_style_border_width(btn_v1, 1, 0);
                        lv_obj_set_style_radius(btn_v1, 6, 0);
                        lv_obj_t* lbl_v1 = lv_label_create(btn_v1);
                        char b1[32];
                        std::snprintf(b1, sizeof(b1), "Giri: %.0f RPM", r.threshold1);
                        lv_label_set_text(lbl_v1, b1);
                        lv_obj_set_style_text_color(lbl_v1, lv_color_hex(0x00FFCC), 0);
                        lv_obj_set_style_text_font(lbl_v1, &lv_font_montserrat_14, 0);
                        lv_obj_center(lbl_v1);
                        lv_obj_add_event_cb(btn_v1, open_num_keypad, LV_EVENT_CLICKED, new ValueEditData{type, false, lbl_v1});

                        // Pulsante Temp Min
                        lv_obj_t* btn_v2 = lv_btn_create(row_c);
                        lv_obj_set_flex_grow(btn_v2, 3);
                        lv_obj_set_height(btn_v2, 38);
                        lv_obj_set_style_bg_color(btn_v2, lv_color_hex(0x1C1C2A), 0);
                        lv_obj_set_style_border_color(btn_v2, lv_color_hex(0x0284C7), 0);
                        lv_obj_set_style_border_width(btn_v2, 1, 0);
                        lv_obj_set_style_radius(btn_v2, 6, 0);
                        lv_obj_t* lbl_v2 = lv_label_create(btn_v2);
                        char b2[32];
                        std::snprintf(b2, sizeof(b2), "Sotto: %.0f \xC2\xB0""C", r.threshold2);
                        lv_label_set_text(lbl_v2, b2);
                        lv_obj_set_style_text_color(lbl_v2, lv_color_hex(0x38BDF8), 0);
                        lv_obj_set_style_text_font(lbl_v2, &lv_font_montserrat_14, 0);
                        lv_obj_center(lbl_v2);
                        lv_obj_add_event_cb(btn_v2, open_num_keypad, LV_EVENT_CLICKED, new ValueEditData{type, true, lbl_v2});
                    } else {
                        // Pulsante Soglia Singola
                        lv_obj_t* btn_v1 = lv_btn_create(row_c);
                        lv_obj_set_flex_grow(btn_v1, 4);
                        lv_obj_set_height(btn_v1, 38);
                        lv_obj_set_style_bg_color(btn_v1, lv_color_hex(0x1C1C2A), 0);
                        lv_obj_set_style_border_color(btn_v1, lv_color_hex(0xCC3030), 0);
                        lv_obj_set_style_border_width(btn_v1, 1, 0);
                        lv_obj_set_style_radius(btn_v1, 6, 0);
                        lv_obj_t* lbl_v1 = lv_label_create(btn_v1);
                        char b1[48];
                        if (type == abarth::config::AlertType::HighBoostPressure) std::snprintf(b1, sizeof(b1), "Soglia: %.2f bar", r.threshold1);
                        else if (type == abarth::config::AlertType::LowBatteryVoltage) std::snprintf(b1, sizeof(b1), "Soglia: %.1f V", r.threshold1);
                        else std::snprintf(b1, sizeof(b1), "Soglia: %.0f \xC2\xB0""C", r.threshold1);
                        lv_label_set_text(lbl_v1, b1);
                        lv_obj_set_style_text_color(lbl_v1, lv_color_hex(0x00FFCC), 0);
                        lv_obj_set_style_text_font(lbl_v1, &lv_font_montserrat_14, 0);
                        lv_obj_center(lbl_v1);
                        lv_obj_add_event_cb(btn_v1, open_num_keypad, LV_EVENT_CLICKED, new ValueEditData{type, false, lbl_v1});
                    }

                    // Chiusura automatica Dropdown
                    lv_obj_t* dd_ac = lv_dropdown_create(row_c);
                    lv_obj_set_flex_grow(dd_ac, 2);
                    lv_obj_set_height(dd_ac, 38);
                    lv_dropdown_set_options(dd_ac, "Solo OK\n3s Auto\n5s Auto\n10s Auto\n15s Auto");
                    style_compact_dropdown(dd_ac, lv_color_hex(0xCC3030));

                    uint16_t sel_idx = 0;
                    if (r.auto_close_sec == 3) sel_idx = 1;
                    else if (r.auto_close_sec == 5) sel_idx = 2;
                    else if (r.auto_close_sec == 10) sel_idx = 3;
                    else if (r.auto_close_sec == 15) sel_idx = 4;
                    lv_dropdown_set_selected(dd_ac, sel_idx);

                    lv_obj_add_event_cb(dd_ac, [](lv_event_t* e) {
                        abarth::config::AlertType t = (abarth::config::AlertType)(uintptr_t)lv_event_get_user_data(e);
                        uint16_t sel = lv_dropdown_get_selected(lv_event_get_target(e));
                        uint8_t sec = (sel == 1 ? 3 : sel == 2 ? 5 : sel == 3 ? 10 : sel == 4 ? 15 : 0);
                        abarth::config::alerts().setRuleAutoClose(t, sec);
                    }, LV_EVENT_VALUE_CHANGED, (void*)(uintptr_t)type);

                    // Pulsante Preview Test
                    lv_obj_t* btn_test = lv_btn_create(row_c);
                    lv_obj_set_flex_grow(btn_test, 2);
                    lv_obj_set_height(btn_test, 38);
                    lv_obj_set_style_bg_color(btn_test, lv_color_hex(0x2A1418), 0);
                    lv_obj_set_style_border_color(btn_test, lv_color_hex(0xFF3333), 0);
                    lv_obj_set_style_border_width(btn_test, 1, 0);
                    lv_obj_set_style_radius(btn_test, 6, 0);

                    lv_obj_t* l_test = lv_label_create(btn_test);
                    lv_label_set_text(l_test, LV_SYMBOL_PLAY " Test Pop-up");
                    lv_obj_set_style_text_color(l_test, lv_color_hex(0xFFDDDD), 0);
                    lv_obj_set_style_text_font(l_test, &lv_font_montserrat_12, 0);
                    lv_obj_center(l_test);

                    lv_obj_add_event_cb(btn_test, [](lv_event_t* e) {
                        abarth::config::AlertType t = (abarth::config::AlertType)(uintptr_t)lv_event_get_user_data(e);
                        auto* rule = abarth::config::alerts().getRule(t);
                        if (rule) {
                            float test1 = rule->threshold1 + (t == abarth::config::AlertType::ColdEngineRpm ? 300.0f : 5.0f);
                            float test2 = rule->threshold2 - 10.0f;
                            abarth::config::alerts().triggerAlertPopup(*rule, test1, test2, true);
                        }
                    }, LV_EVENT_CLICKED, (void*)(uintptr_t)type);
                }
            } else if (subtab_idx == 1) {
                // ==================== SUBMENU 1: AVVISI PERSONALIZZATI ====================
                // Pulsante Nuovo Avviso
                lv_obj_t* btn_add = lv_btn_create(sub_content);
                lv_obj_set_width(btn_add, LV_PCT(100));
                lv_obj_set_height(btn_add, 42);
                lv_obj_set_style_bg_color(btn_add, lv_color_hex(0x008866), 0);
                lv_obj_set_style_radius(btn_add, 8, 0);
                lv_obj_t* l_add = lv_label_create(btn_add);
                lv_label_set_text(l_add, "+ Crea Nuovo Allarme Personalizzato");
                lv_obj_set_style_text_color(l_add, lv_color_hex(0xFFFFFF), 0);
                lv_obj_set_style_text_font(l_add, &lv_font_montserrat_14, 0);
                lv_obj_center(l_add);

                lv_obj_add_event_cb(btn_add, [](lv_event_t* /*e*/) {
                    abarth::config::alerts().addCustomRule();
                    render_category(g_current_cat, 1);
                }, LV_EVENT_CLICKED, nullptr);

                auto c_rules = abarth::config::alerts().customRules();
                if (c_rules.empty()) {
                    lv_obj_t* empty_card = make_card_group(sub_content, "Allarmi Consigliati con 1 Click", LV_SYMBOL_EDIT);
                    lv_obj_set_style_pad_all(empty_card, 12, 0);
                    lv_obj_set_style_pad_row(empty_card, 10, 0);

                    lv_obj_t* empty_lbl = lv_label_create(empty_card);
                    lv_label_set_text(empty_lbl,
                        "Nessun allarme personalizzato attivo. Clicca su uno dei modelli rapidi per attivarlo all'istante, oppure crea una regola libera col tasto sopra:");
                    lv_obj_set_style_text_color(empty_lbl, lv_color_hex(0x9A9AA6), 0);
                    lv_obj_set_style_text_font(empty_lbl, &lv_font_montserrat_12, 0);
                    lv_label_set_long_mode(empty_lbl, LV_LABEL_LONG_WRAP);
                    lv_obj_set_width(empty_lbl, LV_PCT(100));

                    // Griglia 2x2 di preset cliccabili che riempiono lo spazio armoniosamente
                    struct PresetDef {
                        const char* name;
                        const char* desc;
                        abarth::data::Metric metric;
                        abarth::config::AlertOp op;
                        float th;
                        const char* msg;
                        uint32_t color;
                    };
                    const PresetDef presets[4] = {
                        {"Overboost Turbo", "Pressione Turbo > 1.65 bar", abarth::data::Metric::BoostRelBar, abarth::config::AlertOp::GreaterThan, 1.65f, "ATTENZIONE: Sovrapressione Turbo!", 0xCC3030},
                        {"Monitor Olio Pista", "Temp. Olio > 115 \xC2\xB0""C", abarth::data::Metric::OilTempC, abarth::config::AlertOp::GreaterThan, 115.0f, "ALLARME: Temp. Olio Elevata!", 0xD97706},
                        {"Tensione Alternatore", "Batteria < 11.8 V", abarth::data::Metric::BatteryVolts, abarth::config::AlertOp::LessThan, 11.8f, "ATTENZIONE: Batteria Scarica / Alternatore!", 0x2563EB},
                        {"Spia di Cambiata", "Regime Giri > 6200 RPM", abarth::data::Metric::Rpm, abarth::config::AlertOp::GreaterThan, 6200.0f, "CAMBIA MARCIA! Fuorigiri!", 0x059669}
                    };

                    for (int r = 0; r < 2; ++r) {
                        lv_obj_t* p_row = lv_obj_create(empty_card);
                        lv_obj_remove_style_all(p_row);
                        lv_obj_set_width(p_row, LV_PCT(100));
                        lv_obj_set_height(p_row, 56);
                        lv_obj_set_flex_flow(p_row, LV_FLEX_FLOW_ROW);
                        lv_obj_set_style_pad_gap(p_row, 10, 0);

                        for (int c = 0; c < 2; ++c) {
                            int p_idx = r * 2 + c;
                            const auto& p = presets[p_idx];

                            lv_obj_t* p_btn = lv_btn_create(p_row);
                            lv_obj_set_flex_grow(p_btn, 1);
                            lv_obj_set_height(p_btn, 54);
                            lv_obj_set_style_bg_color(p_btn, lv_color_hex(0x181826), 0);
                            lv_obj_set_style_border_color(p_btn, lv_color_hex(p.color), 0);
                            lv_obj_set_style_border_width(p_btn, 1, 0);
                            lv_obj_set_style_radius(p_btn, 8, 0);
                            lv_obj_set_flex_flow(p_btn, LV_FLEX_FLOW_COLUMN);
                            lv_obj_set_flex_align(p_btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
                            lv_obj_set_style_pad_hor(p_btn, 12, 0);

                            lv_obj_t* l_pn = lv_label_create(p_btn);
                            char b_pn[64];
                            std::snprintf(b_pn, sizeof(b_pn), "+ %s", p.name);
                            lv_label_set_text(l_pn, b_pn);
                            lv_obj_set_style_text_color(l_pn, lv_color_hex(0xFFFFFF), 0);
                            lv_obj_set_style_text_font(l_pn, &lv_font_montserrat_14, 0);

                            lv_obj_t* l_pd = lv_label_create(p_btn);
                            lv_label_set_text(l_pd, p.desc);
                            lv_obj_set_style_text_color(l_pd, lv_color_hex(0x00FFCC), 0);
                            lv_obj_set_style_text_font(l_pd, &lv_font_montserrat_12, 0);

                            lv_obj_add_event_cb(p_btn, [](lv_event_t* e) {
                                int idx = (int)(uintptr_t)lv_event_get_user_data(e);
                                const PresetDef defs[4] = {
                                    {"Overboost Turbo", "Pressione Turbo > 1.65 bar", abarth::data::Metric::BoostRelBar, abarth::config::AlertOp::GreaterThan, 1.65f, "ATTENZIONE: Sovrapressione Turbo!", 0xCC3030},
                                    {"Monitor Olio Pista", "Temp. Olio > 115 \xC2\xB0""C", abarth::data::Metric::OilTempC, abarth::config::AlertOp::GreaterThan, 115.0f, "ALLARME: Temp. Olio Elevata!", 0xD97706},
                                    {"Tensione Alternatore", "Batteria < 11.8 V", abarth::data::Metric::BatteryVolts, abarth::config::AlertOp::LessThan, 11.8f, "ATTENZIONE: Batteria Scarica / Alternatore!", 0x2563EB},
                                    {"Spia di Cambiata", "Regime Giri > 6200 RPM", abarth::data::Metric::Rpm, abarth::config::AlertOp::GreaterThan, 6200.0f, "CAMBIA MARCIA! Fuorigiri!", 0x059669}
                                };
                                if (idx >= 0 && idx < 4) {
                                    auto* cr = abarth::config::alerts().addCustomRule();
                                    if (cr) {
                                        cr->name = defs[idx].name;
                                        cr->metric = defs[idx].metric;
                                        cr->op = defs[idx].op;
                                        cr->threshold = defs[idx].th;
                                        cr->custom_text = defs[idx].msg;
                                        cr->auto_close_sec = 5;
                                        abarth::config::alerts().saveCustomRules();
                                        render_category(g_current_cat, 1);
                                    }
                                }
                            }, LV_EVENT_CLICKED, (void*)(uintptr_t)p_idx);
                        }
                    }
                } else {
                    for (const auto& cr : c_rules) {
                        uint8_t cid = cr.id;
                        lv_obj_t* card = make_card_group(sub_content, cr.name.c_str(), LV_SYMBOL_EDIT);
                        lv_obj_set_style_pad_all(card, 12, 0);
                        lv_obj_set_style_pad_row(card, 8, 0);

                        // Switch abilitazione nell'header della card
                        lv_obj_t* h_row = lv_obj_get_child(card, 0);
                        lv_obj_t* sw = lv_switch_create(h_row);
                        lv_obj_set_style_bg_color(sw, lv_color_hex(0x00CC66), (lv_style_selector_t)((uint32_t)LV_PART_INDICATOR | (uint32_t)LV_STATE_CHECKED));
                        if (cr.enabled) lv_obj_add_state(sw, LV_STATE_CHECKED);
                        lv_obj_add_event_cb(sw, [](lv_event_t* e) {
                            uint8_t id = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
                            auto* rule = abarth::config::alerts().getCustomRule(id);
                            if (rule) {
                                rule->enabled = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
                                abarth::config::alerts().saveCustomRules();
                            }
                        }, LV_EVENT_VALUE_CHANGED, (void*)(uintptr_t)cid);

                        // Riga 1: Definizione Condizione (Sensore, Operatore, Valore) a larghezza piena
                        lv_obj_t* row_c1 = lv_obj_create(card);
                        lv_obj_remove_style_all(row_c1);
                        lv_obj_set_width(row_c1, LV_PCT(100));
                        lv_obj_set_height(row_c1, 38);
                        lv_obj_set_flex_flow(row_c1, LV_FLEX_FLOW_ROW);
                        lv_obj_set_flex_align(row_c1, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
                        lv_obj_set_style_pad_gap(row_c1, 8, 0);

                        lv_obj_t* l_se = lv_label_create(row_c1);
                        lv_label_set_text(l_se, "Se:");
                        lv_obj_set_style_text_color(l_se, lv_color_hex(0xCCCCCC), 0);
                        lv_obj_set_style_text_font(l_se, &lv_font_montserrat_14, 0);

                        lv_obj_t* dd_m = lv_dropdown_create(row_c1);
                        lv_obj_set_flex_grow(dd_m, 4);
                        lv_obj_set_height(dd_m, 38);
                        lv_dropdown_set_options(dd_m, "Regime Motore (RPM)\nVelocita' (Km/h)\nTemp. Acqua (C)\nTemp. Olio (C)\nTemp. Aspirazione (C)\nPressione Turbo (bar)\nTensione Batteria (V)");
                        style_compact_dropdown(dd_m, lv_color_hex(0x008866));
                        uint16_t m_sel = 0;
                        if (cr.metric == abarth::data::Metric::Speed) m_sel = 1;
                        else if (cr.metric == abarth::data::Metric::CoolantTempC) m_sel = 2;
                        else if (cr.metric == abarth::data::Metric::OilTempC) m_sel = 3;
                        else if (cr.metric == abarth::data::Metric::IntakeAirTempC) m_sel = 4;
                        else if (cr.metric == abarth::data::Metric::BoostRelBar) m_sel = 5;
                        else if (cr.metric == abarth::data::Metric::BatteryVolts) m_sel = 6;
                        lv_dropdown_set_selected(dd_m, m_sel);
                        lv_obj_add_event_cb(dd_m, [](lv_event_t* e) {
                            uint8_t id = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
                            uint16_t sel = lv_dropdown_get_selected(lv_event_get_target(e));
                            auto* rule = abarth::config::alerts().getCustomRule(id);
                            if (rule) {
                                if (sel == 0) rule->metric = abarth::data::Metric::Rpm;
                                else if (sel == 1) rule->metric = abarth::data::Metric::Speed;
                                else if (sel == 2) rule->metric = abarth::data::Metric::CoolantTempC;
                                else if (sel == 3) rule->metric = abarth::data::Metric::OilTempC;
                                else if (sel == 4) rule->metric = abarth::data::Metric::IntakeAirTempC;
                                else if (sel == 5) rule->metric = abarth::data::Metric::BoostRelBar;
                                else if (sel == 6) rule->metric = abarth::data::Metric::BatteryVolts;
                                abarth::config::alerts().saveCustomRules();
                            }
                        }, LV_EVENT_VALUE_CHANGED, (void*)(uintptr_t)cid);

                        lv_obj_t* dd_op = lv_dropdown_create(row_c1);
                        lv_obj_set_flex_grow(dd_op, 1);
                        lv_obj_set_height(dd_op, 38);
                        lv_dropdown_set_options(dd_op, ">\n<\n==");
                        style_compact_dropdown(dd_op, lv_color_hex(0x008866));
                        lv_dropdown_set_selected(dd_op, static_cast<uint16_t>(cr.op));
                        lv_obj_add_event_cb(dd_op, [](lv_event_t* e) {
                            uint8_t id = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
                            uint16_t sel = lv_dropdown_get_selected(lv_event_get_target(e));
                            auto* rule = abarth::config::alerts().getCustomRule(id);
                            if (rule) {
                                rule->op = static_cast<abarth::config::AlertOp>(sel);
                                abarth::config::alerts().saveCustomRules();
                            }
                        }, LV_EVENT_VALUE_CHANGED, (void*)(uintptr_t)cid);

                        lv_obj_t* btn_th = lv_btn_create(row_c1);
                        lv_obj_set_flex_grow(btn_th, 2);
                        lv_obj_set_height(btn_th, 38);
                        lv_obj_set_style_bg_color(btn_th, lv_color_hex(0x1C1C2A), 0);
                        lv_obj_set_style_border_color(btn_th, lv_color_hex(0x008866), 0);
                        lv_obj_set_style_border_width(btn_th, 1, 0);
                        lv_obj_set_style_radius(btn_th, 6, 0);

                        lv_obj_t* lbl_th = lv_label_create(btn_th);
                        char th_buf[32];
                        std::snprintf(th_buf, sizeof(th_buf), "Soglia: %.1f", cr.threshold);
                        lv_label_set_text(lbl_th, th_buf);
                        lv_obj_set_style_text_color(lbl_th, lv_color_hex(0x00FFCC), 0);
                        lv_obj_set_style_text_font(lbl_th, &lv_font_montserrat_14, 0);
                        lv_obj_center(lbl_th);

                        struct CustomThData { uint8_t cid; lv_obj_t* lbl; };
                        lv_obj_add_event_cb(btn_th, [](lv_event_t* e) {
                            auto* data = static_cast<CustomThData*>(lv_event_get_user_data(e));
                            auto* rule = abarth::config::alerts().getCustomRule(data->cid);
                            if (!rule) return;

                            close_modal(nullptr);
                            lv_obj_t* modal = create_modal("Imposta Soglia Personalizzata");
                            lv_obj_t* ta = lv_textarea_create(modal);
                            lv_obj_set_width(ta, LV_PCT(100));
                            lv_textarea_set_one_line(ta, true);
                            char ibuf[32];
                            std::snprintf(ibuf, sizeof(ibuf), "%.1f", rule->threshold);
                            lv_textarea_set_text(ta, ibuf);
                            lv_obj_set_style_text_font(ta, &lv_font_montserrat_20, 0);

                            lv_obj_t* kb = lv_keyboard_create(modal);
                            lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_NUMBER);
                            lv_keyboard_set_textarea(kb, ta);
                            lv_obj_set_size(kb, LV_PCT(100), LV_PCT(60));

                            struct SaveCustomCtx { uint8_t cid; lv_obj_t* ta; lv_obj_t* target_lbl; };
                            SaveCustomCtx* sctx = new SaveCustomCtx{data->cid, ta, data->lbl};

                            lv_obj_t* btn_save = lv_btn_create(modal);
                            lv_obj_set_width(btn_save, LV_PCT(100));
                            lv_obj_set_height(btn_save, 44);
                            lv_obj_set_style_bg_color(btn_save, lv_color_hex(0x008866), 0);
                            lv_obj_set_style_radius(btn_save, 10, 0);

                            lv_obj_t* l_s = lv_label_create(btn_save);
                            lv_label_set_text(l_s, "Salva Soglia");
                            lv_obj_set_style_text_color(l_s, lv_color_hex(0xFFFFFF), 0);
                            lv_obj_set_style_text_font(l_s, &lv_font_montserrat_16, 0);
                            lv_obj_center(l_s);

                            lv_obj_add_event_cb(btn_save, [](lv_event_t* ev) {
                                auto* c = static_cast<SaveCustomCtx*>(lv_event_get_user_data(ev));
                                auto* r = abarth::config::alerts().getCustomRule(c->cid);
                                if (r) {
                                    r->threshold = std::atof(lv_textarea_get_text(c->ta));
                                    abarth::config::alerts().saveCustomRules();
                                    char b[32];
                                    std::snprintf(b, sizeof(b), "Soglia: %.1f", r->threshold);
                                    lv_label_set_text(c->target_lbl, b);
                                }
                                delete c;
                                close_modal(nullptr);
                            }, LV_EVENT_CLICKED, sctx);
                        }, LV_EVENT_CLICKED, new CustomThData{cid, lbl_th});

                        // Riga 2: Messaggio, Durata & Azioni a larghezza piena
                        lv_obj_t* row_c2 = lv_obj_create(card);
                        lv_obj_remove_style_all(row_c2);
                        lv_obj_set_width(row_c2, LV_PCT(100));
                        lv_obj_set_height(row_c2, 38);
                        lv_obj_set_flex_flow(row_c2, LV_FLEX_FLOW_ROW);
                        lv_obj_set_flex_align(row_c2, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
                        lv_obj_set_style_pad_gap(row_c2, 8, 0);

                        struct CustomTextEditCtx { uint8_t cid; bool is_title; lv_obj_t* lbl; };
                        auto open_text_modal = [](lv_event_t* e) {
                            auto* c = static_cast<CustomTextEditCtx*>(lv_event_get_user_data(e));
                            auto* rule = abarth::config::alerts().getCustomRule(c->cid);
                            if (!rule) return;

                            close_modal(nullptr);
                            lv_obj_t* modal = create_modal(c->is_title ? "Modifica Titolo Avviso" : "Modifica Messaggio Pop-up");

                            lv_obj_t* ta = lv_textarea_create(modal);
                            lv_obj_set_width(ta, LV_PCT(100));
                            lv_textarea_set_one_line(ta, c->is_title);
                            if (!c->is_title) lv_obj_set_height(ta, 70);
                            const std::string& cur_txt = c->is_title ? rule->name : rule->custom_text;
                            lv_textarea_set_text(ta, cur_txt.c_str());
                            lv_obj_set_style_text_font(ta, &lv_font_montserrat_16, 0);

                            lv_obj_t* kb = lv_keyboard_create(modal);
                            lv_keyboard_set_textarea(kb, ta);
                            lv_obj_set_size(kb, LV_PCT(100), LV_PCT(55));

                            struct SaveTextData { uint8_t cid; bool is_title; lv_obj_t* ta; };
                            SaveTextData* st_data = new SaveTextData{c->cid, c->is_title, ta};

                            lv_obj_t* btn_save = lv_btn_create(modal);
                            lv_obj_set_width(btn_save, LV_PCT(100));
                            lv_obj_set_height(btn_save, 44);
                            lv_obj_set_style_bg_color(btn_save, lv_color_hex(0x008866), 0);
                            lv_obj_set_style_radius(btn_save, 10, 0);

                            lv_obj_t* l_s = lv_label_create(btn_save);
                            lv_label_set_text(l_s, "Salva Testo");
                            lv_obj_set_style_text_color(l_s, lv_color_hex(0xFFFFFF), 0);
                            lv_obj_set_style_text_font(l_s, &lv_font_montserrat_16, 0);
                            lv_obj_center(l_s);

                            lv_obj_add_event_cb(btn_save, [](lv_event_t* ev) {
                                auto* st = static_cast<SaveTextData*>(lv_event_get_user_data(ev));
                                auto* r = abarth::config::alerts().getCustomRule(st->cid);
                                if (r) {
                                    const char* txt = lv_textarea_get_text(st->ta);
                                    if (st->is_title) r->name = txt;
                                    else r->custom_text = txt;
                                    abarth::config::alerts().saveCustomRules();
                                    render_category(g_current_cat, 1);
                                }
                                delete st;
                                close_modal(nullptr);
                            }, LV_EVENT_CLICKED, st_data);
                        };

                        lv_obj_t* btn_tx = lv_btn_create(row_c2);
                        lv_obj_set_flex_grow(btn_tx, 4);
                        lv_obj_set_height(btn_tx, 38);
                        lv_obj_set_style_bg_color(btn_tx, lv_color_hex(0x1C1C2A), 0);
                        lv_obj_set_style_border_color(btn_tx, lv_color_hex(0x008866), 0);
                        lv_obj_set_style_border_width(btn_tx, 1, 0);
                        lv_obj_set_style_radius(btn_tx, 6, 0);
                        lv_obj_t* lbl_tx = lv_label_create(btn_tx);
                        lv_obj_set_width(lbl_tx, LV_PCT(92));
                        lv_label_set_long_mode(lbl_tx, LV_LABEL_LONG_DOT);
                        char m_buf[64];
                        std::snprintf(m_buf, sizeof(m_buf), "Msg: %s", cr.custom_text.c_str());
                        lv_label_set_text(lbl_tx, m_buf);
                        lv_obj_set_style_text_color(lbl_tx, lv_color_hex(0x00FFCC), 0);
                        lv_obj_set_style_text_font(lbl_tx, &lv_font_montserrat_12, 0);
                        lv_obj_center(lbl_tx);
                        lv_obj_add_event_cb(btn_tx, open_text_modal, LV_EVENT_CLICKED, new CustomTextEditCtx{cid, false, lbl_tx});

                        lv_obj_t* dd_cac = lv_dropdown_create(row_c2);
                        lv_obj_set_flex_grow(dd_cac, 2);
                        lv_obj_set_height(dd_cac, 38);
                        lv_dropdown_set_options(dd_cac, "Solo OK\n3s Auto\n5s Auto\n10s Auto\n15s Auto");
                        style_compact_dropdown(dd_cac, lv_color_hex(0x008866));

                        uint16_t csel_idx = 0;
                        if (cr.auto_close_sec == 3) csel_idx = 1;
                        else if (cr.auto_close_sec == 5) csel_idx = 2;
                        else if (cr.auto_close_sec == 10) csel_idx = 3;
                        else if (cr.auto_close_sec == 15) csel_idx = 4;
                        lv_dropdown_set_selected(dd_cac, csel_idx);

                        lv_obj_add_event_cb(dd_cac, [](lv_event_t* e) {
                            uint8_t id = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
                            uint16_t sel = lv_dropdown_get_selected(lv_event_get_target(e));
                            auto* rule = abarth::config::alerts().getCustomRule(id);
                            if (rule) {
                                rule->auto_close_sec = (sel == 1 ? 3 : sel == 2 ? 5 : sel == 3 ? 10 : sel == 4 ? 15 : 0);
                                abarth::config::alerts().saveCustomRules();
                            }
                        }, LV_EVENT_VALUE_CHANGED, (void*)(uintptr_t)cid);

                        lv_obj_t* btn_ctest = lv_btn_create(row_c2);
                        lv_obj_set_flex_grow(btn_ctest, 2);
                        lv_obj_set_height(btn_ctest, 38);
                        lv_obj_set_style_bg_color(btn_ctest, lv_color_hex(0x112A22), 0);
                        lv_obj_set_style_border_color(btn_ctest, lv_color_hex(0x008866), 0);
                        lv_obj_set_style_border_width(btn_ctest, 1, 0);
                        lv_obj_set_style_radius(btn_ctest, 6, 0);
                        lv_obj_t* l_ctest = lv_label_create(btn_ctest);
                        lv_label_set_text(l_ctest, LV_SYMBOL_PLAY " Test");
                        lv_obj_set_style_text_color(l_ctest, lv_color_hex(0xDDFFDD), 0);
                        lv_obj_set_style_text_font(l_ctest, &lv_font_montserrat_12, 0);
                        lv_obj_center(l_ctest);

                        lv_obj_add_event_cb(btn_ctest, [](lv_event_t* e) {
                            uint8_t id = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
                            auto* rule = abarth::config::alerts().getCustomRule(id);
                            if (rule) {
                                abarth::config::alerts().triggerCustomAlertPopup(*rule, rule->threshold + 10.0f, true);
                            }
                        }, LV_EVENT_CLICKED, (void*)(uintptr_t)cid);

                        lv_obj_t* btn_del = lv_btn_create(row_c2);
                        lv_obj_set_flex_grow(btn_del, 2);
                        lv_obj_set_height(btn_del, 38);
                        lv_obj_set_style_bg_color(btn_del, lv_color_hex(0x3B1515), 0);
                        lv_obj_set_style_border_color(btn_del, lv_color_hex(0xEF4444), 0);
                        lv_obj_set_style_border_width(btn_del, 1, 0);
                        lv_obj_set_style_radius(btn_del, 6, 0);
                        lv_obj_t* l_del = lv_label_create(btn_del);
                        lv_label_set_text(l_del, LV_SYMBOL_TRASH " Elimina");
                        lv_obj_set_style_text_color(l_del, lv_color_hex(0xFFCCCC), 0);
                        lv_obj_set_style_text_font(l_del, &lv_font_montserrat_12, 0);
                        lv_obj_center(l_del);

                        lv_obj_add_event_cb(btn_del, [](lv_event_t* e) {
                            uint8_t id = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
                            abarth::config::alerts().removeCustomRule(id);
                            render_category(g_current_cat, 1);
                        }, LV_EVENT_CLICKED, (void*)(uintptr_t)cid);
                    }
                }
            } else if (subtab_idx == 2) {
                // ==================== SUBMENU 2: WARMUP & OLIO VIRTUALE ====================
                using namespace abarth::warmup;
                const WarmupState wst = warmup_mgr().state();

                // Card 1: Stato & Switch Warmup
                lv_obj_t* w_status_card = make_card_group(sub_content, "Stato Warmup & Olio Virtuale", LV_SYMBOL_REFRESH);
                lv_obj_set_style_pad_all(w_status_card, 14, 0);
                lv_obj_set_style_pad_row(w_status_card, 10, 0);

                // Riga dedicata per lo stato live
                lv_obj_t* row_status = lv_obj_create(w_status_card);
                lv_obj_remove_style_all(row_status);
                lv_obj_set_width(row_status, LV_PCT(100));
                lv_obj_set_height(row_status, 44);
                lv_obj_set_style_bg_color(row_status, lv_color_hex(0x181826), 0);
                lv_obj_set_style_bg_opa(row_status, LV_OPA_COVER, 0);
                lv_obj_set_style_border_color(row_status, lv_color_hex(0x28283C), 0);
                lv_obj_set_style_border_width(row_status, 1, 0);
                lv_obj_set_style_radius(row_status, 8, 0);
                lv_obj_set_style_pad_hor(row_status, 14, 0);
                lv_obj_set_flex_flow(row_status, LV_FLEX_FLOW_ROW);
                lv_obj_set_flex_align(row_status, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

                lv_obj_t* lbl_st_title = lv_label_create(row_status);
                lv_label_set_text(lbl_st_title, "Stato Termico Attuale:");
                lv_obj_set_style_text_color(lbl_st_title, lv_color_hex(0xFFFFFF), 0);
                lv_obj_set_style_text_font(lbl_st_title, &lv_font_montserrat_14, 0);

                lv_obj_t* badge_live = lv_obj_create(row_status);
                lv_obj_remove_style_all(badge_live);
                lv_obj_set_size(badge_live, LV_SIZE_CONTENT, 30);
                lv_obj_set_style_radius(badge_live, 6, 0);
                lv_obj_set_style_pad_hor(badge_live, 12, 0);
                lv_obj_set_style_pad_ver(badge_live, 4, 0);

                lv_obj_t* lbl_st_val = lv_label_create(badge_live);
                lv_obj_set_style_text_font(lbl_st_val, &lv_font_montserrat_12, 0);

                if (wst == WarmupState::Cold) {
                    lv_obj_set_style_bg_color(badge_live, lv_color_hex(0x0C1929), 0);
                    lv_obj_set_style_border_color(badge_live, lv_color_hex(0x0284C7), 0);
                    lv_obj_set_style_border_width(badge_live, 1, 0);
                    lv_label_set_text(lbl_st_val, LV_SYMBOL_REFRESH " FREDDO");
                    lv_obj_set_style_text_color(lbl_st_val, lv_color_hex(0x38BDF8), 0);
                } else if (wst == WarmupState::Warming) {
                    lv_obj_set_style_bg_color(badge_live, lv_color_hex(0x241406), 0);
                    lv_obj_set_style_border_color(badge_live, lv_color_hex(0xD97706), 0);
                    lv_obj_set_style_border_width(badge_live, 1, 0);
                    char buf_w[32];
                    uint16_t rem = warmup_mgr().remainingSeconds();
                    std::snprintf(buf_w, sizeof(buf_w), LV_SYMBOL_SETTINGS " WARMUP (%02d:%02d)", rem / 60, rem % 60);
                    lv_label_set_text(lbl_st_val, buf_w);
                    lv_obj_set_style_text_color(lbl_st_val, lv_color_hex(0xF59E0B), 0);
                } else if (wst == WarmupState::Ready) {
                    lv_obj_set_style_bg_color(badge_live, lv_color_hex(0x081C10), 0);
                    lv_obj_set_style_border_color(badge_live, lv_color_hex(0x16A34A), 0);
                    lv_obj_set_style_border_width(badge_live, 1, 0);
                    lv_label_set_text(lbl_st_val, LV_SYMBOL_OK " A REGIME");
                    lv_obj_set_style_text_color(lbl_st_val, lv_color_hex(0x22C55E), 0);
                } else {
                    lv_obj_set_style_bg_color(badge_live, lv_color_hex(0x222230), 0);
                    lv_label_set_text(lbl_st_val, "DISATTIVATO");
                    lv_obj_set_style_text_color(lbl_st_val, lv_color_hex(0x888888), 0);
                }

                lv_obj_t* w_desc = lv_label_create(w_status_card);
                lv_label_set_text(w_desc,
                    "Virtualizza la temperatura dell'olio motore combinando la temperatura del refrigerante con un timer di stabilizzazione termica per proteggere a freddo ed evidenziare il regime ottimale.");
                lv_obj_set_style_text_color(w_desc, lv_color_hex(0x9A9AA6), 0);
                lv_obj_set_style_text_font(w_desc, &lv_font_montserrat_12, 0);
                lv_label_set_long_mode(w_desc, LV_LABEL_LONG_WRAP);
                lv_obj_set_width(w_desc, LV_PCT(100));

                // Switch 1 a larghezza piena
                lv_obj_t* row_sw1 = lv_obj_create(w_status_card);
                lv_obj_remove_style_all(row_sw1);
                lv_obj_set_width(row_sw1, LV_PCT(100));
                lv_obj_set_height(row_sw1, 48);
                lv_obj_set_style_bg_color(row_sw1, lv_color_hex(0x181826), 0);
                lv_obj_set_style_bg_opa(row_sw1, LV_OPA_COVER, 0);
                lv_obj_set_style_border_color(row_sw1, lv_color_hex(0x28283C), 0);
                lv_obj_set_style_border_width(row_sw1, 1, 0);
                lv_obj_set_style_radius(row_sw1, 8, 0);
                lv_obj_set_style_pad_hor(row_sw1, 14, 0);
                lv_obj_set_flex_flow(row_sw1, LV_FLEX_FLOW_ROW);
                lv_obj_set_flex_align(row_sw1, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

                lv_obj_t* l_sw_w = lv_label_create(row_sw1);
                lv_label_set_text(l_sw_w, "Abilita Protezione Warmup e Limiti Giri");
                lv_obj_set_style_text_color(l_sw_w, lv_color_hex(0xFFFFFF), 0);
                lv_obj_set_style_text_font(l_sw_w, &lv_font_montserrat_14, 0);

                lv_obj_t* sw_w = lv_switch_create(row_sw1);
                lv_obj_set_style_bg_color(sw_w, lv_color_hex(0x00CC66), (lv_style_selector_t)((uint32_t)LV_PART_INDICATOR | (uint32_t)LV_STATE_CHECKED));
                if (s.oil_warmup_enabled) lv_obj_add_state(sw_w, LV_STATE_CHECKED);
                lv_obj_add_event_cb(sw_w, [](lv_event_t* e) {
                    bool en = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
                    abarth::settings::setOilWarmupEnabled(en);
                }, LV_EVENT_VALUE_CHANGED, nullptr);

                // Switch 2 a larghezza piena
                lv_obj_t* row_sw2 = lv_obj_create(w_status_card);
                lv_obj_remove_style_all(row_sw2);
                lv_obj_set_width(row_sw2, LV_PCT(100));
                lv_obj_set_height(row_sw2, 48);
                lv_obj_set_style_bg_color(row_sw2, lv_color_hex(0x181826), 0);
                lv_obj_set_style_bg_opa(row_sw2, LV_OPA_COVER, 0);
                lv_obj_set_style_border_color(row_sw2, lv_color_hex(0x28283C), 0);
                lv_obj_set_style_border_width(row_sw2, 1, 0);
                lv_obj_set_style_radius(row_sw2, 8, 0);
                lv_obj_set_style_pad_hor(row_sw2, 14, 0);
                lv_obj_set_flex_flow(row_sw2, LV_FLEX_FLOW_ROW);
                lv_obj_set_flex_align(row_sw2, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

                lv_obj_t* l_sw_vd = lv_label_create(row_sw2);
                lv_label_set_text(l_sw_vd, "Mostra Temperatura Olio Virtuale in Dash");
                lv_obj_set_style_text_color(l_sw_vd, lv_color_hex(0xFFFFFF), 0);
                lv_obj_set_style_text_font(l_sw_vd, &lv_font_montserrat_14, 0);

                lv_obj_t* sw_vd = lv_switch_create(row_sw2);
                lv_obj_set_style_bg_color(sw_vd, lv_color_hex(0x00CC66), (lv_style_selector_t)((uint32_t)LV_PART_INDICATOR | (uint32_t)LV_STATE_CHECKED));
                if (s.oil_virtual_temp_display) lv_obj_add_state(sw_vd, LV_STATE_CHECKED);
                lv_obj_add_event_cb(sw_vd, [](lv_event_t* e) {
                    bool en = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
                    abarth::settings::setOilVirtualTempDisplay(en);
                }, LV_EVENT_VALUE_CHANGED, nullptr);

                // Card 2: Parametri e Soglie Regolabili (Righe a tutta larghezza anti-sovrapposizione)
                lv_obj_t* w_param_card = make_card_group(sub_content, "Parametri e Soglie di Calcolo", LV_SYMBOL_SETTINGS);
                lv_obj_set_style_pad_all(w_param_card, 14, 0);
                lv_obj_set_style_pad_row(w_param_card, 10, 0);

                enum class WarmupParamType { WaterTemp, CountdownSec, ColdRpmLimit };
                struct WarmupEditCtx { WarmupParamType ptype; lv_obj_t* lbl_target; };

                auto open_warmup_keypad = [](lv_event_t* e) {
                    auto* ctx = static_cast<WarmupEditCtx*>(lv_event_get_user_data(e));
                    close_modal(nullptr);

                    const char* title = "Imposta Valore";
                    float cur_val = 0.0f;
                    const auto& cur_s = abarth::settings::current();
                    if (ctx->ptype == WarmupParamType::WaterTemp) {
                        title = "Soglia Temp. Acqua (\xC2\xB0""C)";
                        cur_val = cur_s.oil_warmup_water_temp;
                    } else if (ctx->ptype == WarmupParamType::CountdownSec) {
                        title = "Tempo Countdown (Secondi)";
                        cur_val = static_cast<float>(cur_s.oil_warmup_countdown_sec);
                    } else if (ctx->ptype == WarmupParamType::ColdRpmLimit) {
                        title = "Soglia Giri a Freddo (RPM)";
                        cur_val = cur_s.oil_cold_rpm_limit;
                    }

                    lv_obj_t* modal = create_modal(title);
                    lv_obj_t* ta = lv_textarea_create(modal);
                    lv_obj_set_width(ta, LV_PCT(100));
                    lv_textarea_set_one_line(ta, true);
                    char ibuf[32];
                    std::snprintf(ibuf, sizeof(ibuf), "%.0f", cur_val);
                    lv_textarea_set_text(ta, ibuf);
                    lv_obj_set_style_text_font(ta, &lv_font_montserrat_20, 0);
                    lv_obj_set_style_text_color(ta, lv_color_hex(0x00FFCC), 0);

                    lv_obj_t* kb = lv_keyboard_create(modal);
                    lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_NUMBER);
                    lv_keyboard_set_textarea(kb, ta);
                    lv_obj_set_size(kb, LV_PCT(100), LV_PCT(60));

                    struct SaveCtx { WarmupParamType ptype; lv_obj_t* ta; lv_obj_t* lbl; };
                    SaveCtx* sc = new SaveCtx{ctx->ptype, ta, ctx->lbl_target};

                    lv_obj_t* btn_save = lv_btn_create(modal);
                    lv_obj_set_width(btn_save, LV_PCT(100));
                    lv_obj_set_height(btn_save, 44);
                    lv_obj_set_style_bg_color(btn_save, lv_color_hex(0xCC3030), 0);
                    lv_obj_set_style_radius(btn_save, 10, 0);
                    lv_obj_t* l_save = lv_label_create(btn_save);
                    lv_label_set_text(l_save, "Salva Valore");
                    lv_obj_set_style_text_color(l_save, lv_color_hex(0xFFFFFF), 0);
                    lv_obj_set_style_text_font(l_save, &lv_font_montserrat_16, 0);
                    lv_obj_center(l_save);

                    lv_obj_add_event_cb(btn_save, [](lv_event_t* ev) {
                        auto* c = static_cast<SaveCtx*>(lv_event_get_user_data(ev));
                        float v = std::atof(lv_textarea_get_text(c->ta));
                        char buf[32];
                        if (c->ptype == WarmupParamType::WaterTemp) {
                            if (v < 50.0f) v = 50.0f;
                            if (v > 100.0f) v = 100.0f;
                            abarth::settings::setOilWarmupWaterTemp(v);
                            std::snprintf(buf, sizeof(buf), "%.0f \xC2\xB0""C", v);
                        } else if (c->ptype == WarmupParamType::CountdownSec) {
                            if (v < 30.0f) v = 30.0f;
                            if (v > 1800.0f) v = 1800.0f;
                            uint16_t s_val = static_cast<uint16_t>(v);
                            abarth::settings::setOilWarmupCountdownSec(s_val);
                            std::snprintf(buf, sizeof(buf), "%u min (%us)", s_val / 60, s_val);
                        } else if (c->ptype == WarmupParamType::ColdRpmLimit) {
                            if (v < 1500.0f) v = 1500.0f;
                            if (v > 6000.0f) v = 6000.0f;
                            abarth::settings::setOilColdRpmLimit(v);
                            std::snprintf(buf, sizeof(buf), "%.0f RPM", v);
                        }
                        lv_label_set_text(c->lbl, buf);
                        delete c;
                        close_modal(nullptr);
                    }, LV_EVENT_CLICKED, sc);
                };

                auto make_param_row = [&](const char* title, const char* sub, const char* init_val, WarmupParamType pt) {
                    lv_obj_t* p_row = lv_obj_create(w_param_card);
                    lv_obj_remove_style_all(p_row);
                    lv_obj_set_width(p_row, LV_PCT(100));
                    lv_obj_set_height(p_row, 54);
                    lv_obj_set_style_bg_color(p_row, lv_color_hex(0x181826), 0);
                    lv_obj_set_style_bg_opa(p_row, LV_OPA_COVER, 0);
                    lv_obj_set_style_border_color(p_row, lv_color_hex(0x28283C), 0);
                    lv_obj_set_style_border_width(p_row, 1, 0);
                    lv_obj_set_style_radius(p_row, 8, 0);
                    lv_obj_set_style_pad_hor(p_row, 14, 0);
                    lv_obj_set_flex_flow(p_row, LV_FLEX_FLOW_ROW);
                    lv_obj_set_flex_align(p_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

                    lv_obj_t* t_box = lv_obj_create(p_row);
                    lv_obj_remove_style_all(t_box);
                    lv_obj_set_flex_grow(t_box, 1);
                    lv_obj_set_height(t_box, LV_SIZE_CONTENT);
                    lv_obj_set_flex_flow(t_box, LV_FLEX_FLOW_COLUMN);
                    lv_obj_set_style_pad_gap(t_box, 2, 0);

                    lv_obj_t* l_t = lv_label_create(t_box);
                    lv_label_set_text(l_t, title);
                    lv_obj_set_style_text_color(l_t, lv_color_hex(0xFFFFFF), 0);
                    lv_obj_set_style_text_font(l_t, &lv_font_montserrat_14, 0);

                    lv_obj_t* l_s = lv_label_create(t_box);
                    lv_label_set_text(l_s, sub);
                    lv_obj_set_style_text_color(l_s, lv_color_hex(0x888899), 0);
                    lv_obj_set_style_text_font(l_s, &lv_font_montserrat_12, 0);

                    lv_obj_t* btn_edit = lv_btn_create(p_row);
                    lv_obj_set_width(btn_edit, 140);
                    lv_obj_set_height(btn_edit, 38);
                    lv_obj_set_style_bg_color(btn_edit, lv_color_hex(0x222234), 0);
                    lv_obj_set_style_border_color(btn_edit, lv_color_hex(0xCC3030), 0);
                    lv_obj_set_style_border_width(btn_edit, 1, 0);
                    lv_obj_set_style_radius(btn_edit, 8, 0);

                    lv_obj_t* lbl_val = lv_label_create(btn_edit);
                    lv_label_set_text(lbl_val, init_val);
                    lv_obj_set_style_text_color(lbl_val, lv_color_hex(0x00FFCC), 0);
                    lv_obj_set_style_text_font(lbl_val, &lv_font_montserrat_14, 0);
                    lv_obj_center(lbl_val);

                    lv_obj_add_event_cb(btn_edit, open_warmup_keypad, LV_EVENT_CLICKED, new WarmupEditCtx{pt, lbl_val});
                };

                char b1[32];
                std::snprintf(b1, sizeof(b1), "%.0f \xC2\xB0""C", s.oil_warmup_water_temp);
                make_param_row("Temperatura Acqua di Regime", "Soglia minima refrigerante per avviare il timer di regime", b1, WarmupParamType::WaterTemp);

                char b2[32];
                std::snprintf(b2, sizeof(b2), "%u min (%us)", s.oil_warmup_countdown_sec / 60, s.oil_warmup_countdown_sec);
                make_param_row("Tempo Stabilizzazione Termica", "Countdown post-riscaldamento prima di considerare l'olio caldo", b2, WarmupParamType::CountdownSec);

                char b3[32];
                std::snprintf(b3, sizeof(b3), "%.0f RPM", s.oil_cold_rpm_limit);
                make_param_row("Limite Giri a Freddo (Safety RPM)", "Giri massimi consigliati prima dell'allarme motore freddo", b3, WarmupParamType::ColdRpmLimit);

                // Card 3: Azioni & Test Rapidi
                lv_obj_t* w_test_card = make_card_group(sub_content, "Simulazione & Anteprima Avvisi", LV_SYMBOL_PLAY);
                lv_obj_set_style_pad_all(w_test_card, 14, 0);
                lv_obj_set_style_pad_row(w_test_card, 10, 0);

                lv_obj_t* row_tests = lv_obj_create(w_test_card);
                lv_obj_remove_style_all(row_tests);
                lv_obj_set_width(row_tests, LV_PCT(100));
                lv_obj_set_height(row_tests, 44);
                lv_obj_set_flex_flow(row_tests, LV_FLEX_FLOW_ROW);
                lv_obj_set_style_pad_gap(row_tests, 10, 0);

                // Test Allarme Motore Freddo
                lv_obj_t* btn_tc = lv_btn_create(row_tests);
                lv_obj_set_flex_grow(btn_tc, 1);
                lv_obj_set_height(btn_tc, 44);
                lv_obj_set_style_bg_color(btn_tc, lv_color_hex(0x3B1818), 0);
                lv_obj_set_style_border_color(btn_tc, lv_color_hex(0xEF4444), 0);
                lv_obj_set_style_border_width(btn_tc, 1, 0);
                lv_obj_set_style_radius(btn_tc, 8, 0);
                lv_obj_t* l_tc = lv_label_create(btn_tc);
                lv_label_set_text(l_tc, LV_SYMBOL_WARNING " Test Allarme Freddo");
                lv_obj_set_style_text_color(l_tc, lv_color_hex(0xFFAAAA), 0);
                lv_obj_set_style_text_font(l_tc, &lv_font_montserrat_12, 0);
                lv_obj_center(l_tc);
                lv_obj_add_event_cb(btn_tc, [](lv_event_t*) {
                    warmup_mgr().triggerColdAlert(3400.0f, 52.0f, true);
                }, LV_EVENT_CLICKED, nullptr);

                // Test Allarme Motore in Temperatura
                lv_obj_t* btn_tr = lv_btn_create(row_tests);
                lv_obj_set_flex_grow(btn_tr, 1);
                lv_obj_set_height(btn_tr, 44);
                lv_obj_set_style_bg_color(btn_tr, lv_color_hex(0x13301C), 0);
                lv_obj_set_style_border_color(btn_tr, lv_color_hex(0x22C55E), 0);
                lv_obj_set_style_border_width(btn_tr, 1, 0);
                lv_obj_set_style_radius(btn_tr, 8, 0);
                lv_obj_t* l_tr = lv_label_create(btn_tr);
                lv_label_set_text(l_tr, LV_SYMBOL_OK " Test Motore a Regime");
                lv_obj_set_style_text_color(l_tr, lv_color_hex(0xAAFFAA), 0);
                lv_obj_set_style_text_font(l_tr, &lv_font_montserrat_12, 0);
                lv_obj_center(l_tr);
                lv_obj_add_event_cb(btn_tr, [](lv_event_t*) {
                    warmup_mgr().triggerReadyAlert(true);
                }, LV_EVENT_CLICKED, nullptr);

                // Reset Ciclo Warmup
                lv_obj_t* btn_res = lv_btn_create(row_tests);
                lv_obj_set_flex_grow(btn_res, 1);
                lv_obj_set_height(btn_res, 44);
                lv_obj_set_style_bg_color(btn_res, lv_color_hex(0x202030), 0);
                lv_obj_set_style_border_color(btn_res, lv_color_hex(0x606080), 0);
                lv_obj_set_style_border_width(btn_res, 1, 0);
                lv_obj_set_style_radius(btn_res, 8, 0);
                lv_obj_t* l_res = lv_label_create(btn_res);
                lv_label_set_text(l_res, LV_SYMBOL_REFRESH " Reset Ciclo");
                lv_obj_set_style_text_color(l_res, lv_color_hex(0xCCCCCC), 0);
                lv_obj_set_style_text_font(l_res, &lv_font_montserrat_12, 0);
                lv_obj_center(l_res);
                lv_obj_add_event_cb(btn_res, [](lv_event_t*) {
                    warmup_mgr().resetCycle();
                    render_category(g_current_cat, 2);
                }, LV_EVENT_CLICKED, nullptr);
            }

            break;
        }

        // === 12: COPYRIGHT, PROPRIETÀ INTELLETTUALE & LICENZA ===
        case 12: {
            auto make_legal_card = [&](const char* title, const char* icon, const char* content, uint32_t border_col) {
                lv_obj_t* card = make_card_group(g_content_panel, title, icon);
                lv_obj_set_style_border_color(card, lv_color_hex(border_col), 0);
                lv_obj_set_style_pad_all(card, 14, 0);
                lv_obj_set_style_pad_row(card, 8, 0);

                lv_obj_t* lbl = lv_label_create(card);
                lv_label_set_text(lbl, content);
                lv_obj_set_style_text_color(lbl, lv_color_hex(0xD1D5DB), 0);
                lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
                lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
                lv_obj_set_width(lbl, LV_PCT(100));
                return card;
            };

            // Card 1: Titolarità Esclusiva & Diritti d'Autore
            make_legal_card(
                "Titolarita' Esclusiva & Diritti d'Autore",
                LV_SYMBOL_FILE,
                "L'intero software e firmware 'Abarth 595 Telemetry & Digital Dashboard', comprensivo di codice sorgente, architettura software, algoritmi di calcolo dinamico, interfaccia grafica (UI/UX), driver bus CAN/OBD-II e documentazione tecnica, e' opera dell'ingegno di proprieta' esclusiva, assoluta ed inalienabile del suo autore e legittimo proprietario.\n\n"
                "Tutti i diritti sono rigorosamente riservati in tutto il mondo conformemente alla Legge 22 aprile 1941 n. 633 sul Diritto d'Autore, alla Direttiva Europea 2009/24/CE sulla tutela giuridica del software per elaboratore, e alla Convenzione di Berna per la protezione delle opere letterarie ed artistiche.",
                0x0D9488
            );

            // Card 2: Termini di Licenza e Divieti d'Uso
            make_legal_card(
                "Termini di Licenza, Restrizioni & Divieti",
                LV_SYMBOL_WARNING,
                "E' concesso l'uso del presente sistema unicamente ed esclusivamente a scopo personale e per il singolo veicolo autorizzato.\n\n"
                "• DIVIETO DI REVERSE ENGINEERING: E' severamente vietata qualunque attivita' di reverse engineering, decompilazione, disassemblaggio, estrazione o dump del firmware binario da Flash/PSRAM.\n"
                "• DIVIETO DI DISTRIBUZIONE O VENDITA: Nessuna porzione di questo software, del suo codice o delle risorse grafiche puo' essere duplicata, riprodotta, commercializzata, ceduta o sub-licenziata a terzi senza formale autorizzazione scritta del proprietario.\n"
                "• TUTELA INTEGRITA' SOFTWARE: E' fatto espresso divieto di alterare, occultare o rimuovere i marchi di attribuzione della titolarita', le intestazioni di copyright o i metadati di autenticazione.",
                0xDC2626
            );

            // Card 3: Know-How e Algoritmi Proprietari
            make_legal_card(
                "Algoritmi Registrati & Know-How Tecnologico",
                LV_SYMBOL_SETTINGS,
                "Questo dispositivo e' basato su innovazioni ingegneristiche e routine proprietarie sviluppate ad hoc, tra cui in via esemplificativa:\n\n"
                "• Modello Matematico di Riscaldamento Olio Motore (Virtual Oil Warmup Estimator)\n"
                "• Pipeline grafica renderizzata a 60 FPS con accelerazione hardware su SoC Espressif ESP32-P4\n"
                "• Driver di comunicazione ottimizzato su bus CAN-Bus e protocolli di telemetria Abarth\n"
                "• Motore di streaming a bassa latenza Bluetooth Low Energy (BLE) per telemetria remota\n\n"
                "Tali architetture, formule matematiche e soluzioni tecniche costituiscono segreto industriale e know-how riservato protetto.",
                0x7C3AED
            );

            // Card 4: Limitazione della Responsabilità (Disclaimer)
            make_legal_card(
                "Limitazione di Responsabilita' (Disclaimer)",
                LV_SYMBOL_CLOSE,
                "Il presente sistema software di telemetria e diagnostica viene fornito 'COSI' COM'E'' ('AS IS'), senza garanzie esplicite o implicite di alcun genere.\n\n"
                "L'autore e titolare non potra' in nessun caso essere ritenuto responsabile per eventuali danni diretti, indiretti, speciali o consequenziali al veicolo, al motore, al cambio, alla centralina ECU o all'impianto elettrico derivanti dall'utilizzo, da errate interpretazioni delle letture o da qualsiasi anomalia funzionale.\n\n"
                "L'applicazione e' concepita unicamente per uso amatoriale e su circuito chiuso (track use). Il conducente e' l'unico e insindacabile responsabile della condotta di guida e del pieno rispetto del Codice della Strada.",
                0xD97706
            );

            // Card 5: Certificazione Autenticità & Metadati Build
            lv_obj_t* cert_card = make_card_group(g_content_panel, "Certificato di Autenticita' Firmware", LV_SYMBOL_OK);
            lv_obj_set_style_border_color(cert_card, lv_color_hex(0x059669), 0);
            lv_obj_set_style_pad_all(cert_card, 14, 0);
            lv_obj_set_style_pad_row(cert_card, 8, 0);

            lv_obj_t* c_row = lv_obj_create(cert_card);
            lv_obj_remove_style_all(c_row);
            lv_obj_set_width(c_row, LV_PCT(100));
            lv_obj_set_height(c_row, 36);
            lv_obj_set_flex_flow(c_row, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(c_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

            lv_obj_t* l_auth = lv_label_create(c_row);
            lv_label_set_text(l_auth, LV_SYMBOL_OK " BUILD UFFICIALE AUTORIZZATA");
            lv_obj_set_style_text_color(l_auth, lv_color_hex(0x22C55E), 0);
            lv_obj_set_style_text_font(l_auth, &lv_font_montserrat_14, 0);

            lv_obj_t* l_cr = lv_label_create(c_row);
            lv_label_set_text(l_cr, "(C) 2026 - Proprieta' Riservata all'Autore");
            lv_obj_set_style_text_color(l_cr, lv_color_hex(0x9CA3AF), 0);
            lv_obj_set_style_text_font(l_cr, &lv_font_montserrat_12, 0);

            lv_obj_t* l_meta = lv_label_create(cert_card);
            char mbuf[256];
            std::snprintf(mbuf, sizeof(mbuf),
                "Progetto: Abarth 595 Telemetry System\n"
                "Target Hardware: Espressif ESP32-P4 RISC-V SoC\n"
                "UI Engine: LVGL Embedded Graphic Framework v8.4\n"
                "Compilazione Ufficiale: %s ore %s",
                __DATE__, __TIME__);
            lv_label_set_text(l_meta, mbuf);
            lv_obj_set_style_text_color(l_meta, lv_color_hex(0x6EE7B7), 0);
            lv_obj_set_style_text_font(l_meta, &lv_font_montserrat_12, 0);
            break;
        }

    }
}

void open_category(uint8_t cat_idx, uint16_t subtab_idx) {
    if (cat_idx >= NUM_CATEGORIES) cat_idx = 0;
    g_current_cat = cat_idx;

    if (g_detail_badge_ico) {
        lv_label_set_text(g_detail_badge_ico, g_categories[cat_idx].icon);
    }
    if (g_detail_badge) {
        lv_obj_set_style_bg_color(g_detail_badge, lv_color_hex(g_categories[cat_idx].badge_color), 0);
    }
    if (g_detail_title_lbl) {
        lv_label_set_text(g_detail_title_lbl, g_categories[cat_idx].title);
    }
    if (g_detail_sub_lbl) {
        lv_label_set_text(g_detail_sub_lbl, g_categories[cat_idx].subtitle);
    }

    render_category(g_current_cat, subtab_idx);

    if (g_menu_page) {
        lv_obj_add_flag(g_menu_page, LV_OBJ_FLAG_HIDDEN);
    }
    if (g_detail_page) {
        lv_obj_clear_flag(g_detail_page, LV_OBJ_FLAG_HIDDEN);
    }
    if (g_content_panel) {
        lv_obj_scroll_to_y(g_content_panel, 0, LV_ANIM_OFF);
    }
}

void close_category() {
    if (g_content_panel) {
        lv_obj_clean(g_content_panel);
    }
    g_lbl_brightness_value = nullptr;
    g_lbl_sysinfo          = nullptr;
    g_lbl_obd_status       = nullptr;
    g_lbl_wifi_status      = nullptr;
    g_lbl_ble_status       = nullptr;
    g_lbl_can1_diag        = nullptr;
    g_lbl_can2_diag        = nullptr;
    g_lbl_counters_info    = nullptr;
    g_lbl_rtc_info         = nullptr;
    g_lbl_rtc_time_display = nullptr;
    g_lbl_rtc_date_display = nullptr;
    g_lbl_rtc_hour_val     = nullptr;
    g_lbl_rtc_min_val      = nullptr;
    g_lbl_rtc_day_val      = nullptr;
    g_lbl_rtc_month_val    = nullptr;
    g_lbl_rtc_year_val     = nullptr;
    g_lbl_stat_device      = nullptr;
    g_lbl_stat_state       = nullptr;
    g_lbl_stat_ok          = nullptr;
    g_lbl_stat_err         = nullptr;
    g_lbl_stat_lasterr     = nullptr;

    if (g_detail_page) {
        lv_obj_add_flag(g_detail_page, LV_OBJ_FLAG_HIDDEN);
    }
    if (g_menu_page) {
        lv_obj_clear_flag(g_menu_page, LV_OBJ_FLAG_HIDDEN);
    }
}

}  // namespace

void build(lv_obj_t* tab) {
    ensure_styles();

    lv_obj_set_style_bg_color(tab, lv_color_hex(0x080810), 0);
    lv_obj_set_style_bg_opa(tab, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(tab, 0, 0);
    lv_obj_remove_style(tab, nullptr, LV_PART_SCROLLBAR);
    lv_obj_clear_flag(tab, LV_OBJ_FLAG_SCROLLABLE);

    const bool is_portrait = (LV_HOR_RES < LV_VER_RES);

    // =========================================================================
    // 1. PAGINA MENU PRINCIPALE (Panoramica Categorie stile Smartphone)
    // =========================================================================
    g_menu_page = lv_obj_create(tab);
    lv_obj_remove_style_all(g_menu_page);
    lv_obj_set_size(g_menu_page, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(g_menu_page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_color(g_menu_page, lv_color_hex(0x0E0E14), 0);
    lv_obj_set_style_bg_opa(g_menu_page, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(g_menu_page, 16, 0);
    lv_obj_set_style_pad_top(g_menu_page, 10, 0);
    lv_obj_set_style_pad_bottom(g_menu_page, 8, 0);
    lv_obj_set_style_pad_row(g_menu_page, 10, 0);
    lv_obj_clear_flag(g_menu_page, LV_OBJ_FLAG_SCROLLABLE);

    // Header Bar in alto nel Menu Categorie (con Titolo, Data e Ora RTC)
    lv_obj_t* header_card = lv_obj_create(g_menu_page);
    lv_obj_remove_style_all(header_card);
    lv_obj_set_width(header_card, LV_PCT(100));
    lv_obj_set_height(header_card, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(header_card, lv_color_hex(0x141420), 0);
    lv_obj_set_style_bg_opa(header_card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(header_card, lv_color_hex(0x28283C), 0);
    lv_obj_set_style_border_width(header_card, 1, 0);
    lv_obj_set_style_radius(header_card, 12, 0);
    lv_obj_set_style_pad_hor(header_card, 16, 0);
    lv_obj_set_style_pad_ver(header_card, 8, 0);
    lv_obj_set_flex_flow(header_card, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header_card, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(header_card, LV_OBJ_FLAG_SCROLLABLE);

    // Lato sinistro: Titolo e icona impostazioni
    lv_obj_t* h_left = lv_obj_create(header_card);
    lv_obj_remove_style_all(h_left);
    lv_obj_set_width(h_left, LV_SIZE_CONTENT);
    lv_obj_set_height(h_left, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(h_left, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(h_left, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(h_left, 12, 0);
    lv_obj_clear_flag(h_left, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* h_badge = lv_obj_create(h_left);
    lv_obj_remove_style_all(h_badge);
    lv_obj_set_size(h_badge, 38, 38);
    lv_obj_set_style_radius(h_badge, 10, 0);
    lv_obj_set_style_bg_color(h_badge, lv_color_hex(0xCC3030), 0);
    lv_obj_set_style_bg_opa(h_badge, LV_OPA_COVER, 0);
    lv_obj_clear_flag(h_badge, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* h_ico = lv_label_create(h_badge);
    lv_label_set_text(h_ico, LV_SYMBOL_SETTINGS);
    lv_obj_set_style_text_color(h_ico, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(h_ico, &lv_font_montserrat_18, 0);
    lv_obj_center(h_ico);
    lv_obj_clear_flag(h_ico, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* h_txt_box = lv_obj_create(h_left);
    lv_obj_remove_style_all(h_txt_box);
    lv_obj_set_width(h_txt_box, LV_SIZE_CONTENT);
    lv_obj_set_height(h_txt_box, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(h_txt_box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(h_txt_box, 1, 0);
    lv_obj_clear_flag(h_txt_box, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* h_title = lv_label_create(h_txt_box);
    lv_label_set_text(h_title, "IMPOSTAZIONI");
    lv_obj_set_style_text_color(h_title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(h_title, &lv_font_montserrat_16, 0);
    lv_obj_clear_flag(h_title, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* h_sub = lv_label_create(h_txt_box);
    lv_label_set_text(h_sub, "Configurazione parametri & veicolo");
    lv_obj_set_style_text_color(h_sub, lv_color_hex(0x8E8E9A), 0);
    lv_obj_set_style_text_font(h_sub, &lv_font_montserrat_12, 0);
    lv_obj_clear_flag(h_sub, LV_OBJ_FLAG_CLICKABLE);

    // Lato destro: Card Data & Ora (Toccabile -> apre categoria 9 "Data, Ora & RTC")
    lv_obj_t* time_card = lv_btn_create(header_card);
    lv_obj_set_width(time_card, LV_SIZE_CONTENT);
    lv_obj_set_height(time_card, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(time_card, lv_color_hex(0x1B1B2A), 0);
    lv_obj_set_style_bg_opa(time_card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(time_card, lv_color_hex(0x35354E), 0);
    lv_obj_set_style_border_width(time_card, 1, 0);
    lv_obj_set_style_radius(time_card, 10, 0);
    lv_obj_set_style_pad_hor(time_card, 14, 0);
    lv_obj_set_style_pad_ver(time_card, 6, 0);
    lv_obj_set_style_bg_color(time_card, lv_color_hex(0x28283E), LV_STATE_PRESSED);
    lv_obj_set_style_border_color(time_card, lv_color_hex(0xEF4444), LV_STATE_PRESSED);
    lv_obj_set_flex_flow(time_card, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(time_card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(time_card, 10, 0);
    lv_obj_clear_flag(time_card, LV_OBJ_FLAG_SCROLLABLE);

    // Icona orologio
    lv_obj_t* t_ico = lv_label_create(time_card);
    lv_label_set_text(t_ico, LV_SYMBOL_REFRESH);
    lv_obj_set_style_text_color(t_ico, lv_color_hex(0x0284C7), 0);
    lv_obj_set_style_text_font(t_ico, &lv_font_montserrat_16, 0);
    lv_obj_clear_flag(t_ico, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(t_ico, LV_OBJ_FLAG_EVENT_BUBBLE);

    // Box orario + data
    lv_obj_t* t_box = lv_obj_create(time_card);
    lv_obj_remove_style_all(t_box);
    lv_obj_set_width(t_box, LV_SIZE_CONTENT);
    lv_obj_set_height(t_box, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(t_box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(t_box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_gap(t_box, 1, 0);
    lv_obj_clear_flag(t_box, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(t_box, LV_OBJ_FLAG_EVENT_BUBBLE);

    g_lbl_menu_time = lv_label_create(t_box);
    lv_label_set_text(g_lbl_menu_time, "--:--:--");
    lv_obj_set_style_text_color(g_lbl_menu_time, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(g_lbl_menu_time, &lv_font_montserrat_18, 0);
    lv_obj_clear_flag(g_lbl_menu_time, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(g_lbl_menu_time, LV_OBJ_FLAG_EVENT_BUBBLE);

    g_lbl_menu_date = lv_label_create(t_box);
    lv_label_set_text(g_lbl_menu_date, "Caricamento...");
    lv_obj_set_style_text_color(g_lbl_menu_date, lv_color_hex(0x9E9EB6), 0);
    lv_obj_set_style_text_font(g_lbl_menu_date, &lv_font_montserrat_12, 0);
    lv_obj_clear_flag(g_lbl_menu_date, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(g_lbl_menu_date, LV_OBJ_FLAG_EVENT_BUBBLE);

    // Freccia destra chevron
    lv_obj_t* t_chev = lv_label_create(time_card);
    lv_label_set_text(t_chev, LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_color(t_chev, lv_color_hex(0x66667A), 0);
    lv_obj_set_style_text_font(t_chev, &lv_font_montserrat_14, 0);
    lv_obj_clear_flag(t_chev, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(t_chev, LV_OBJ_FLAG_EVENT_BUBBLE);

    lv_obj_add_event_cb(time_card, [](lv_event_t*) {
        open_category(9, 0); // Apri "Data, Ora & RTC"
    }, LV_EVENT_CLICKED, nullptr);

    // Griglia Categorie (2 colonne in landscape, 1 colonna in portrait)
    lv_obj_t* grid_cont = lv_obj_create(g_menu_page);
    g_menu_grid = grid_cont;
    lv_obj_remove_style_all(grid_cont);
    lv_obj_set_width(grid_cont, LV_PCT(100));
    lv_obj_set_flex_grow(grid_cont, 1);
    lv_obj_set_flex_flow(grid_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_color(grid_cont, lv_color_hex(0x0E0E14), 0);
    lv_obj_set_style_bg_opa(grid_cont, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_row(grid_cont, 12, 0);
    lv_obj_set_style_pad_bottom(grid_cont, 16, 0);
    lv_obj_set_scroll_dir(grid_cont, LV_DIR_VER);
    lv_obj_add_flag(grid_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(grid_cont, LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_clear_flag(grid_cont, LV_OBJ_FLAG_SCROLL_CHAIN_HOR);
    lv_obj_set_scrollbar_mode(grid_cont, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(grid_cont, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* cur_row = nullptr;

    for (size_t i = 0; i < NUM_CATEGORIES; ++i) {
        lv_obj_t* parent_cont = grid_cont;
        if (!is_portrait) {
            if (i % 2 == 0) {
                cur_row = lv_obj_create(grid_cont);
                lv_obj_remove_style_all(cur_row);
                lv_obj_set_width(cur_row, LV_PCT(100));
                lv_obj_set_height(cur_row, LV_SIZE_CONTENT);
                lv_obj_set_flex_flow(cur_row, LV_FLEX_FLOW_ROW);
                lv_obj_set_style_pad_gap(cur_row, 14, 0);
                lv_obj_clear_flag(cur_row, LV_OBJ_FLAG_CLICKABLE);
                lv_obj_clear_flag(cur_row, LV_OBJ_FLAG_SCROLLABLE);
            }
            parent_cont = cur_row;
        }

        lv_obj_t* card = lv_obj_create(parent_cont);
        lv_obj_remove_style_all(card);
        lv_obj_add_style(card, &style_menu_card, 0);
        lv_obj_add_style(card, &style_menu_card_pressed, LV_STATE_PRESSED);
        lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        if (is_portrait) {
            lv_obj_set_width(card, LV_PCT(100));
        } else {
            lv_obj_set_flex_grow(card, 1);
        }
        lv_obj_set_height(card, 70);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_hor(card, 14, 0);
        lv_obj_set_style_pad_gap(card, 14, 0);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

        // 1. Badge icona colorata
        lv_obj_t* badge = lv_obj_create(card);
        lv_obj_remove_style_all(badge);
        lv_obj_set_size(badge, 44, 44);
        lv_obj_set_style_radius(badge, 10, 0);
        lv_obj_set_style_bg_color(badge, lv_color_hex(g_categories[i].badge_color), 0);
        lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
        lv_obj_clear_flag(badge, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(badge, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(badge, LV_OBJ_FLAG_EVENT_BUBBLE);

        lv_obj_t* ico = lv_label_create(badge);
        lv_label_set_text(ico, g_categories[i].icon);
        lv_obj_set_style_text_color(ico, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_font(ico, &lv_font_montserrat_20, 0);
        lv_obj_center(ico);
        lv_obj_clear_flag(ico, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(ico, LV_OBJ_FLAG_EVENT_BUBBLE);

        // 2. Testi: Titolo + Sottotitolo
        lv_obj_t* txt_cont = lv_obj_create(card);
        lv_obj_remove_style_all(txt_cont);
        lv_obj_set_flex_grow(txt_cont, 1);
        lv_obj_set_height(txt_cont, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(txt_cont, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_gap(txt_cont, 2, 0);
        lv_obj_clear_flag(txt_cont, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(txt_cont, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(txt_cont, LV_OBJ_FLAG_EVENT_BUBBLE);

        lv_obj_t* t_lbl = lv_label_create(txt_cont);
        lv_label_set_text(t_lbl, g_categories[i].title);
        lv_label_set_long_mode(t_lbl, LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_color(t_lbl, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_font(t_lbl, &lv_font_montserrat_16, 0);
        lv_obj_clear_flag(t_lbl, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(t_lbl, LV_OBJ_FLAG_EVENT_BUBBLE);

        lv_obj_t* s_lbl = lv_label_create(txt_cont);
        lv_label_set_text(s_lbl, g_categories[i].subtitle);
        lv_label_set_long_mode(s_lbl, LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_color(s_lbl, lv_color_hex(0x9A9AA6), 0);
        lv_obj_set_style_text_font(s_lbl, &lv_font_montserrat_12, 0);
        lv_obj_clear_flag(s_lbl, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(s_lbl, LV_OBJ_FLAG_EVENT_BUBBLE);

        // 3. Freccia chevron destra
        lv_obj_t* chev = lv_label_create(card);
        lv_label_set_text(chev, LV_SYMBOL_RIGHT);
        lv_obj_set_style_text_color(chev, lv_color_hex(0x66667A), 0);
        lv_obj_set_style_text_font(chev, &lv_font_montserrat_16, 0);
        lv_obj_set_style_pad_right(chev, 4, 0);
        lv_obj_clear_flag(chev, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(chev, LV_OBJ_FLAG_EVENT_BUBBLE);

        // Callback apertura categoria
        lv_obj_add_event_cb(card, [](lv_event_t* e) {
            uintptr_t cat_idx = (uintptr_t)lv_event_get_user_data(e);
            open_category(static_cast<uint8_t>(cat_idx), 0);
        }, LV_EVENT_CLICKED, (void*)(uintptr_t)i);
    }

    // =========================================================================
    // 2. PAGINA DETTAGLIO CATEGORIA (Navigazione Top Bar + Full Width Panel)
    // =========================================================================
    g_detail_page = lv_obj_create(tab);
    lv_obj_remove_style_all(g_detail_page);
    lv_obj_set_size(g_detail_page, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(g_detail_page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_color(g_detail_page, lv_color_hex(0x0E0E14), 0);
    lv_obj_set_style_bg_opa(g_detail_page, LV_OPA_COVER, 0);
    lv_obj_clear_flag(g_detail_page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(g_detail_page, LV_OBJ_FLAG_HIDDEN); // Nascosta di default

    // Top Navigation Bar (Fissa in alto, non scrolla)
    lv_obj_t* nav_bar = lv_obj_create(g_detail_page);
    lv_obj_remove_style_all(nav_bar);
    lv_obj_set_width(nav_bar, LV_PCT(100));
    lv_obj_set_height(nav_bar, 52);
    lv_obj_set_style_bg_color(nav_bar, lv_color_hex(0x111118), 0);
    lv_obj_set_style_bg_opa(nav_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(nav_bar, lv_color_hex(0x28283C), 0);
    lv_obj_set_style_border_side(nav_bar, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(nav_bar, 1, 0);
    lv_obj_set_style_pad_hor(nav_bar, 14, 0);
    lv_obj_set_style_pad_ver(nav_bar, 4, 0);
    lv_obj_set_flex_flow(nav_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(nav_bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(nav_bar, 16, 0);
    lv_obj_clear_flag(nav_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(nav_bar, LV_OBJ_FLAG_CLICKABLE);

    // Pulsante Indietro [ < Categorie ]
    lv_obj_t* btn_back = lv_btn_create(nav_bar);
    lv_obj_set_height(btn_back, 40);
    lv_obj_set_width(btn_back, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(btn_back, lv_color_hex(0x20202E), 0);
    lv_obj_set_style_bg_opa(btn_back, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(btn_back, lv_color_hex(0x3E3E56), 0);
    lv_obj_set_style_border_width(btn_back, 1, 0);
    lv_obj_set_style_radius(btn_back, 10, 0);
    lv_obj_set_style_pad_hor(btn_back, 16, 0);
    lv_obj_set_style_pad_ver(btn_back, 6, 0);
    lv_obj_set_style_bg_color(btn_back, lv_color_hex(0x35354A), LV_STATE_PRESSED);
    lv_obj_set_style_border_color(btn_back, lv_color_hex(0xEF4444), LV_STATE_PRESSED);
    lv_obj_set_flex_flow(btn_back, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_back, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(btn_back, 8, 0);

    lv_obj_t* b_ico = lv_label_create(btn_back);
    lv_label_set_text(b_ico, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(b_ico, lv_color_hex(0xEF4444), 0);
    lv_obj_set_style_text_font(b_ico, &lv_font_montserrat_16, 0);
    lv_obj_clear_flag(b_ico, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(b_ico, LV_OBJ_FLAG_EVENT_BUBBLE);

    lv_obj_t* b_lbl = lv_label_create(btn_back);
    lv_label_set_text(b_lbl, "Categorie");
    lv_obj_set_style_text_color(b_lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(b_lbl, &lv_font_montserrat_14, 0);
    lv_obj_clear_flag(b_lbl, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(b_lbl, LV_OBJ_FLAG_EVENT_BUBBLE);

    lv_obj_add_event_cb(btn_back, [](lv_event_t*) {
        close_category();
    }, LV_EVENT_CLICKED, nullptr);

    // Badge icona categoria in Navbar
    g_detail_badge = lv_obj_create(nav_bar);
    lv_obj_remove_style_all(g_detail_badge);
    lv_obj_set_size(g_detail_badge, 36, 36);
    lv_obj_set_style_radius(g_detail_badge, 8, 0);
    lv_obj_set_style_bg_color(g_detail_badge, lv_color_hex(0x0284C7), 0);
    lv_obj_set_style_bg_opa(g_detail_badge, LV_OPA_COVER, 0);
    lv_obj_clear_flag(g_detail_badge, LV_OBJ_FLAG_CLICKABLE);

    g_detail_badge_ico = lv_label_create(g_detail_badge);
    lv_label_set_text(g_detail_badge_ico, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_color(g_detail_badge_ico, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(g_detail_badge_ico, &lv_font_montserrat_16, 0);
    lv_obj_center(g_detail_badge_ico);
    lv_obj_clear_flag(g_detail_badge_ico, LV_OBJ_FLAG_CLICKABLE);

    // Testi in Navbar
    lv_obj_t* d_txt_box = lv_obj_create(nav_bar);
    lv_obj_remove_style_all(d_txt_box);
    lv_obj_set_flex_grow(d_txt_box, 1);
    lv_obj_set_height(d_txt_box, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(d_txt_box, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(d_txt_box, LV_OBJ_FLAG_CLICKABLE);

    g_detail_title_lbl = lv_label_create(d_txt_box);
    lv_label_set_text(g_detail_title_lbl, "Categoria");
    lv_label_set_long_mode(g_detail_title_lbl, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_color(g_detail_title_lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(g_detail_title_lbl, &lv_font_montserrat_16, 0);
    lv_obj_clear_flag(g_detail_title_lbl, LV_OBJ_FLAG_CLICKABLE);

    g_detail_sub_lbl = lv_label_create(d_txt_box);
    lv_label_set_text(g_detail_sub_lbl, "Descrizione categoria");
    lv_label_set_long_mode(g_detail_sub_lbl, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_color(g_detail_sub_lbl, lv_color_hex(0x8E8E9A), 0);
    lv_obj_set_style_text_font(g_detail_sub_lbl, &lv_font_montserrat_12, 0);
    lv_obj_clear_flag(g_detail_sub_lbl, LV_OBJ_FLAG_CLICKABLE);

    // Orario e Data compatti in alto a destra nella Navbar
    lv_obj_t* d_time_box = lv_obj_create(nav_bar);
    lv_obj_remove_style_all(d_time_box);
    lv_obj_set_width(d_time_box, LV_SIZE_CONTENT);
    lv_obj_set_height(d_time_box, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(d_time_box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(d_time_box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_right(d_time_box, 6, 0);
    lv_obj_clear_flag(d_time_box, LV_OBJ_FLAG_CLICKABLE);

    g_lbl_detail_time = lv_label_create(d_time_box);
    lv_label_set_text(g_lbl_detail_time, "--:--:--");
    lv_obj_set_style_text_color(g_lbl_detail_time, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(g_lbl_detail_time, &lv_font_montserrat_16, 0);
    lv_obj_clear_flag(g_lbl_detail_time, LV_OBJ_FLAG_CLICKABLE);

    g_lbl_detail_date = lv_label_create(d_time_box);
    lv_label_set_text(g_lbl_detail_date, "--/--/----");
    lv_obj_set_style_text_color(g_lbl_detail_date, lv_color_hex(0x8E8E9A), 0);
    lv_obj_set_style_text_font(g_lbl_detail_date, &lv_font_montserrat_12, 0);
    lv_obj_clear_flag(g_lbl_detail_date, LV_OBJ_FLAG_CLICKABLE);

    // Pannello Contenuto Categoria (Flex Grow 1, Larghezza 100%, Scrollabile)
    g_content_panel = lv_obj_create(g_detail_page);
    lv_obj_remove_style_all(g_content_panel);
    lv_obj_set_flex_grow(g_content_panel, 1);
    lv_obj_set_width(g_content_panel, LV_PCT(100));
    lv_obj_set_style_bg_color(g_content_panel, lv_color_hex(0x0E0E14), 0);
    lv_obj_set_style_bg_opa(g_content_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(g_content_panel, 16, 0);
    lv_obj_set_style_pad_row(g_content_panel, 14, 0);
    lv_obj_set_flex_flow(g_content_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(g_content_panel, LV_DIR_VER);
    lv_obj_add_flag(g_content_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(g_content_panel, LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_clear_flag(g_content_panel, LV_OBJ_FLAG_SCROLL_CHAIN_HOR);
    lv_obj_set_scrollbar_mode(g_content_panel, LV_SCROLLBAR_MODE_OFF);

    // Inizializza subito data e ora correnti su tutti i widget
    refresh_rtc_ui();
}

void tick() {
    static int64_t last_sysinfo_ms = 0;
    const int64_t  now_ms          = esp_timer_get_time() / 1000;

    if ((now_ms - last_sysinfo_ms) >= 1000) {
        last_sysinfo_ms = now_ms;

        // Aggiorna data e ora sul menu principale delle impostazioni
        if (g_lbl_menu_time || g_lbl_menu_date) {
            refresh_rtc_ui();
        }

        // Se la pagina di dettaglio e' chiusa o nascosta, i widget interni sono nulli
        if (g_detail_page && lv_obj_has_flag(g_detail_page, LV_OBJ_FLAG_HIDDEN)) {
            return;
        }
        
        // 1. Sysinfo label
        if (g_lbl_sysinfo) {
            esp_chip_info_t chip_info;
            esp_chip_info(&chip_info);
            const size_t free_heap = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
            const size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
            
            char buf[320];
            std::snprintf(buf, sizeof(buf),
                "SoC: ESP32-P4 (Rev %d) | IDF: %s\n"
                "SRAM Libera: %u KB | PSRAM Libera: %u KB\n"
                "Partizione Firmware Attiva: %s\n"
                "Prossima Partizione OTA: %s\n"
                "Firmware Build: %s %s",
                chip_info.revision,
                esp_get_idf_version(),
                static_cast<unsigned>(free_heap / 1024),
                static_cast<unsigned>(free_psram / 1024),
                abarth::ota::ota().getCurrentPartitionName().c_str(),
                abarth::ota::ota().getNextPartitionName().c_str(),
                __DATE__, __TIME__);
            lv_label_set_text(g_lbl_sysinfo, buf);
        }

        // 1b. RTC Live Widgets Refresh (Header menu, navbar e pagina RTC)
        if (g_lbl_rtc_info || g_lbl_rtc_time_display || g_lbl_menu_time || g_lbl_detail_time) {
            refresh_rtc_ui();
        }

        // 2. OBD Readings Counters
        if (g_lbl_counters_info) {
            const uint32_t ok  = abarth::obd::obd().successfulReads();
            const uint32_t err = abarth::obd::obd().failedReads();
            lv_label_set_text_fmt(g_lbl_counters_info, "Letture CAN/OBD Riuscite: %lu  |  Errori: %lu",
                                  static_cast<unsigned long>(ok),
                                  static_cast<unsigned long>(err));
        }

        // 3. OBD Status
        if (g_lbl_obd_status) {
            auto state = abarth::obd::obd().state();
            lv_label_set_text_fmt(g_lbl_obd_status, "Stato OBD: %s", abarth::obd::stateName(state));
        }

        // 4. WiFi Status
        if (g_lbl_wifi_status) {
            auto& wm = abarth::wifi::WifiManager::instance();
            if (wm.isConnected()) {
                lv_label_set_text_fmt(g_lbl_wifi_status, 
                    "WiFi: %s\nIP: %s | Gateway: %s\nNetmask: %s | DNS: %s", 
                    wm.getCurrentSSID().c_str(), 
                    wm.getCurrentIP().c_str(),
                    wm.getCurrentGateway().c_str(),
                    wm.getCurrentNetmask().c_str(),
                    wm.getCurrentDNS().c_str());
                lv_obj_set_style_text_color(g_lbl_wifi_status, lv_color_hex(0x00FF00), 0);
            } else {
                lv_label_set_text(g_lbl_wifi_status, "WiFi: Disconnesso");
                lv_obj_set_style_text_color(g_lbl_wifi_status, lv_color_hex(0xAAAAAA), 0);
            }
        }

        // 5. BLE Status
        if (g_lbl_ble_status) {
            const auto& s = abarth::settings::current();
            if (!s.saved_dongle_mac.empty()) {
                lv_label_set_text_fmt(g_lbl_ble_status, "BLE MAC: %s", s.saved_dongle_mac.c_str());
                lv_obj_set_style_text_color(g_lbl_ble_status, lv_color_hex(0x00AADD), 0);
            } else {
                lv_label_set_text(g_lbl_ble_status, "BLE: Disconnesso");
                lv_obj_set_style_text_color(g_lbl_ble_status, lv_color_hex(0xAAAAAA), 0);
            }
        }

        // 6. Monitor Live Dual CAN-Bus Diretto
        if (g_lbl_can1_diag) {
            char buf[192];
            snprintf(buf, sizeof(buf),
                "Stato CAN 1 (C-CAN 500k): %s\n"
                "RX: %lu frames (%.1f fps)  |  TX: %lu\n"
                "Errori: %lu  |  %s",
                abarth::can::canStateName(abarth::can::can().state(0)),
                static_cast<unsigned long>(abarth::can::can().rxFrames(0)),
                abarth::can::can().rxRateFps(0),
                static_cast<unsigned long>(abarth::can::can().txFrames(0)),
                static_cast<unsigned long>(abarth::can::can().busErrors(0)),
                abarth::can::can().lastError(0).empty() ? "Nessun errore (OK)" : abarth::can::can().lastError(0).c_str());
            lv_label_set_text(g_lbl_can1_diag, buf);
        }

        if (g_lbl_can2_diag) {
            char buf[192];
            snprintf(buf, sizeof(buf),
                "Stato CAN 2 (B-CAN 50k): %s\n"
                "RX: %lu frames (%.1f fps)  |  TX: %lu\n"
                "Errori: %lu  |  %s",
                abarth::can::canStateName(abarth::can::can().state(1)),
                static_cast<unsigned long>(abarth::can::can().rxFrames(1)),
                abarth::can::can().rxRateFps(1),
                static_cast<unsigned long>(abarth::can::can().txFrames(1)),
                static_cast<unsigned long>(abarth::can::can().busErrors(1)),
                abarth::can::can().lastError(1).empty() ? "Nessun errore (OK)" : abarth::can::can().lastError(1).c_str());
            lv_label_set_text(g_lbl_can2_diag, buf);
        }

        // 7. Stato Connessione & Diagnostica Vettura (Categoria 8 Info Sistema & Stato)
        const auto& s = abarth::settings::current();
        const bool is_can = (s.connection_mode == abarth::settings::ConnectionMode::DirectCan);

        if (g_lbl_stat_device) {
            std::string dev_str = "Dispositivo: ";
            if (is_can) {
                char cbuf[128];
                snprintf(cbuf, sizeof(cbuf), "Dual CAN: C-CAN (G%u/%u @ %luk) | B-CAN (G%u/%u @ %luk)",
                         s.can1_tx_pin, s.can1_rx_pin, static_cast<unsigned long>(s.can1_baud_rate / 1000),
                         s.can2_tx_pin, s.can2_rx_pin, static_cast<unsigned long>(s.can2_baud_rate / 1000));
                dev_str += cbuf;
            } else if (!s.saved_dongle_mac.empty()) {
                dev_str += "BLE (" + s.saved_dongle_mac + ")";
            } else if (!s.saved_wifi_ssid.empty()) {
                dev_str += "WiFi (" + s.saved_wifi_ssid + ")";
            } else {
                dev_str += "BLE / WiFi (Ricerca Automatica)";
            }
            lv_label_set_text(g_lbl_stat_device, dev_str.c_str());
        }

        if (g_lbl_stat_state) {
            const bool is_demo = s.demo_mode;
            lv_color_t color = lv_color_hex(0x9A9AA6);

            if (is_demo) {
                lv_label_set_text(g_lbl_stat_state, "[ SIMULAZIONE ATTIVA ]");
                color = lv_color_hex(0xFFD700);
            } else if (is_can) {
                const auto c1_st = abarth::can::can().state(0);
                const auto c2_st = abarth::can::can().state(1);
                char s_buf[64];
                if (c1_st == abarth::can::CanState::Running && c2_st == abarth::can::CanState::Running) {
                    snprintf(s_buf, sizeof(s_buf), "DUAL CAN ATTIVO (OK)");
                    color = lv_color_hex(0x30CC60);
                } else if (c1_st == abarth::can::CanState::Running) {
                    snprintf(s_buf, sizeof(s_buf), "C-CAN 500K ATTIVO (OK)");
                    color = lv_color_hex(0x30CC60);
                } else if (c2_st == abarth::can::CanState::Running) {
                    snprintf(s_buf, sizeof(s_buf), "B-CAN 50K ATTIVO (OK)");
                    color = lv_color_hex(0x30CC60);
                } else if (c1_st == abarth::can::CanState::BusOff || c2_st == abarth::can::CanState::BusOff ||
                           c1_st == abarth::can::CanState::Error || c2_st == abarth::can::CanState::Error) {
                    snprintf(s_buf, sizeof(s_buf), "CAN BUS-OFF / ERRORE");
                    color = lv_color_hex(0xFF5252);
                } else {
                    snprintf(s_buf, sizeof(s_buf), "CAN ARRESTATO");
                    color = lv_color_hex(0x9A9AA6);
                }
                lv_label_set_text(g_lbl_stat_state, s_buf);
            } else {
                const auto st = abarth::obd::obd().state();
                lv_label_set_text(g_lbl_stat_state, abarth::obd::stateName(st));
                switch (st) {
                    case abarth::obd::ObdState::Polling:
                        color = lv_color_hex(0x30CC60);
                        break;
                    case abarth::obd::ObdState::Connecting:
                    case abarth::obd::ObdState::Initializing:
                    case abarth::obd::ObdState::Scanning:
                        color = lv_color_hex(0xFFC107);
                        break;
                    case abarth::obd::ObdState::Error:
                        color = lv_color_hex(0xFF5252);
                        break;
                    default:
                        color = lv_color_hex(0x9A9AA6);
                        break;
                }
            }
            lv_obj_set_style_text_color(g_lbl_stat_state, color, 0);
        }

        if (g_lbl_stat_ok) {
            const uint32_t ok_val = is_can ? abarth::can::can().totalRxFrames() : abarth::obd::obd().successfulReads();
            lv_label_set_text_fmt(g_lbl_stat_ok, "%lu", static_cast<unsigned long>(ok_val));
        }
        if (g_lbl_stat_err) {
            const uint32_t err_val = is_can ? abarth::can::can().totalErrors() : abarth::obd::obd().failedReads();
            lv_label_set_text_fmt(g_lbl_stat_err, "%lu", static_cast<unsigned long>(err_val));
        }
        if (g_lbl_stat_lasterr) {
            std::string err_str;
            if (is_can) {
                if (!abarth::can::can().lastError(0).empty()) {
                    err_str = "CAN1: " + abarth::can::can().lastError(0);
                } else if (!abarth::can::can().lastError(1).empty()) {
                    err_str = "CAN2: " + abarth::can::can().lastError(1);
                }
            } else {
                err_str = abarth::obd::obd().lastError();
            }
            if (!err_str.empty()) {
                lv_label_set_text(g_lbl_stat_lasterr, err_str.c_str());
                lv_obj_set_style_text_color(g_lbl_stat_lasterr, lv_color_hex(0xFF5252), 0);
            } else {
                lv_label_set_text(g_lbl_stat_lasterr, "Nessun errore rilevato");
                lv_obj_set_style_text_color(g_lbl_stat_lasterr, lv_color_hex(0x9A9AA6), 0);
            }
        }
    }
}

lv_obj_t* scroll_target() { return g_menu_grid; }

}  // namespace abarth::ui::settings_tab
