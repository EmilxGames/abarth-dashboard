#include "ui/splash.h"

#include <lvgl.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_system.h>
#include <esp_random.h>
#include <sys/stat.h>
#include <cstring>

#include "config/settings.h"
#include "display/lvgl_v8_port.h"
#include "storage/sd_card_manager.h"
#include "storage/rtc_manager.h"
#include "ui/abarth_splash_png.h"

namespace abarth::ui::splash {

namespace {
constexpr const char* TAG = "splash";

lv_obj_t*   g_screen       = nullptr;
lv_obj_t*   g_bar          = nullptr;
lv_obj_t*   g_status_label = nullptr;
lv_obj_t*   g_pct_label    = nullptr;
lv_timer_t* g_progress_tmr = nullptr;

int32_t g_progress = 0;
static bool g_dismiss_requested = false;

static const char* kAbarthQuotes[] = {
    "\"C'e' del gusto ad umiliare vetture di cilindrata superiore.\" - Carlo Abarth",
    "\"La domenica in pista, il lunedi' in ufficio.\" - Carlo Abarth",
    "Scorpione in azione: 1.4 Turbo T-Jet pronto al decollo!",
    "Garrett GT1446 in pressione. Adrenalina al massimo!",
    "Scarico Record Monza aperto: sound inconfondibile.",
    "Assetto Koni FSD pronto per le curve dell'Appennino.",
    "Abarth: trasformiamo l'ordinario in straordinario.",
    "Cuore 595 Competizione: 180 CV di pura passione!",
    "Freni Brembo a 4 pistoncini: staccata al limite!"
};

void progress_cb(lv_timer_t* /*t*/) {
    if (!g_bar) return;
    g_progress += 1;
    if (g_progress > 100) g_progress = 100;
    lv_bar_set_value(g_bar, g_progress, LV_ANIM_ON);

    if (g_pct_label) {
        char pbuf[16];
        std::snprintf(pbuf, sizeof(pbuf), "%d%%", (int)g_progress);
        lv_label_set_text(g_pct_label, pbuf);
    }
}

void screen_clicked_cb(lv_event_t* /*e*/) {
    ESP_LOGI(TAG, "Tocco sullo splash: skip richiesto dall'utente");
    requestDismiss();
}

}  // namespace

void show() {
    if (!lvgl_port_lock(-1)) {
        ESP_LOGE(TAG, "lvgl_port_lock fallito");
        return;
    }

    g_dismiss_requested = false;

    g_screen = lv_obj_create(nullptr);
    lv_obj_remove_style_all(g_screen);
    lv_obj_set_size(g_screen, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(g_screen, lv_color_hex(0x07070B), 0);
    lv_obj_set_style_bg_opa(g_screen, LV_OPA_COVER, 0);
    lv_obj_add_flag(g_screen, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(g_screen, screen_clicked_cb, LV_EVENT_CLICKED, nullptr);

    const bool is_portrait = (LV_HOR_RES < LV_VER_RES);
    uint32_t r = esp_random();
    const char* random_quote = kAbarthQuotes[r % (sizeof(kAbarthQuotes) / sizeof(kAbarthQuotes[0]))];

    // =========================================================================
    // 1. SFONDO PERSONALIZZATO DA MICROSD (SE PRESENTE)
    // =========================================================================
    const char* custom_bg_path = nullptr;
    if (abarth::storage::sd().isMounted()) {
        struct stat st;
        if (stat("/sdcard/splash.png", &st) == 0) {
            custom_bg_path = "S:/splash.png";
        } else if (stat("/sdcard/backgrounds/splash.png", &st) == 0) {
            custom_bg_path = "S:/backgrounds/splash.png";
        } else if (stat("/sdcard/boot.png", &st) == 0) {
            custom_bg_path = "S:/boot.png";
        }
    }

    if (custom_bg_path) {
        lv_obj_t* bg_img = lv_image_create(g_screen);
        lv_image_set_src(bg_img, custom_bg_path);
        lv_obj_center(bg_img);
        lv_obj_clear_flag(bg_img, LV_OBJ_FLAG_CLICKABLE);

        // Velo scuro per garantire massima leggibilità di testi e icone
        lv_obj_t* overlay = lv_obj_create(g_screen);
        lv_obj_remove_style_all(overlay);
        lv_obj_set_size(overlay, LV_HOR_RES, LV_VER_RES);
        lv_obj_set_style_bg_color(overlay, lv_color_hex(0x050508), 0);
        lv_obj_set_style_bg_opa(overlay, LV_OPA_60, 0);
        lv_obj_clear_flag(overlay, LV_OBJ_FLAG_CLICKABLE);
    }

    // =========================================================================
    // 2. FASCIA TRICOLORE DA CORSA (Verde Corsa - Bianco - Rosso Scuderia)
    // =========================================================================
    lv_obj_t* stripe_bg = lv_obj_create(g_screen);
    lv_obj_remove_style_all(stripe_bg);
    lv_obj_set_size(stripe_bg, is_portrait ? LV_PCT(80) : 740, 7);
    lv_obj_align(stripe_bg, LV_ALIGN_TOP_MID, 0, is_portrait ? 40 : 18);
    lv_obj_set_flex_flow(stripe_bg, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_all(stripe_bg, 0, 0);
    lv_obj_set_style_pad_column(stripe_bg, 0, 0);
    lv_obj_set_style_radius(stripe_bg, 4, 0);
    lv_obj_set_style_clip_corner(stripe_bg, true, 0);
    lv_obj_clear_flag(stripe_bg, LV_OBJ_FLAG_CLICKABLE);

    auto add_stripe = [&](uint32_t color) {
        lv_obj_t* s = lv_obj_create(stripe_bg);
        lv_obj_remove_style_all(s);
        lv_obj_set_flex_grow(s, 1);
        lv_obj_set_height(s, LV_PCT(100));
        lv_obj_set_style_bg_color(s, lv_color_hex(color), 0);
        lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
        lv_obj_clear_flag(s, LV_OBJ_FLAG_CLICKABLE);
    };
    add_stripe(0x009246);  // Verde Italia Corsa
    add_stripe(0xF4F4F4);  // Bianco Competizione
    add_stripe(0xCE2B37);  // Rosso Scuderia Abarth

    // =========================================================================
    // 3. CENTRO SCENICO: VERO SCUDETTO ABARTH IN ALTA DEFINIZIONE
    // =========================================================================
    const int badge_y_offset = is_portrait ? -110 : -58;

    // Disco nero di montaggio con bordo sportivo
    lv_obj_t* disc = lv_obj_create(g_screen);
    lv_obj_remove_style_all(disc);
    lv_obj_set_size(disc, 206, 206);
    lv_obj_align(disc, LV_ALIGN_CENTER, 0, badge_y_offset);
    lv_obj_set_style_radius(disc, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(disc, lv_color_hex(0x0F0F16), 0);
    lv_obj_set_style_bg_opa(disc, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(disc, 2, 0);
    lv_obj_set_style_border_color(disc, lv_color_hex(0x282838), 0);
    lv_obj_clear_flag(disc, LV_OBJ_FLAG_CLICKABLE);

    // Arco da corsa animato rotante attorno allo scudetto
    lv_obj_t* spinner = lv_spinner_create(g_screen);
    lv_spinner_set_anim_params(spinner, 2200, 75);
    lv_obj_set_size(spinner, 220, 220);
    lv_obj_align(spinner, LV_ALIGN_CENTER, 0, badge_y_offset);
    lv_obj_set_style_arc_color(spinner, lv_color_hex(0x181824), LV_PART_MAIN);
    lv_obj_set_style_arc_color(spinner, lv_color_hex(0xEF4444), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(spinner, 4, LV_PART_MAIN);
    lv_obj_set_style_arc_width(spinner, 4, LV_PART_INDICATOR);
    lv_obj_clear_flag(spinner, LV_OBJ_FLAG_CLICKABLE);

    // Emblema Scudetto Abarth Ufficiale (da MicroSD se presente o da Flash interna)
    lv_obj_t* emblem = lv_image_create(g_screen);
    if (abarth::storage::sd().isMounted()) {
        lv_image_set_src(emblem, "S:/icons/abarth_splash.png");
    } else {
        lv_image_set_src(emblem, &g_abarth_splash_img_dsc);
    }
    lv_obj_align(emblem, LV_ALIGN_CENTER, 0, badge_y_offset);
    lv_obj_clear_flag(emblem, LV_OBJ_FLAG_CLICKABLE);

    // =========================================================================
    // 4. TIPOGRAFIA BRAND & MODELLO
    // =========================================================================
    const int title_y = is_portrait ? 60 : 62;
    lv_obj_t* title = lv_label_create(g_screen);
    lv_label_set_text(title, "ABARTH");
    lv_obj_set_style_text_color(title, lv_color_hex(0xEF4444), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_48, 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, title_y);
    lv_obj_clear_flag(title, LV_OBJ_FLAG_CLICKABLE);

    const int sub_y = is_portrait ? 106 : 104;
    lv_obj_t* subtitle = lv_label_create(g_screen);
    lv_label_set_text(subtitle, "595 COMPETIZIONE");
    lv_obj_set_style_text_color(subtitle, lv_color_hex(0xF3F4F6), 0);
    lv_obj_set_style_text_font(subtitle, &lv_font_montserrat_16, 0);
    lv_obj_align(subtitle, LV_ALIGN_CENTER, 0, sub_y);
    lv_obj_clear_flag(subtitle, LV_OBJ_FLAG_CLICKABLE);

    const int tag_y = is_portrait ? 128 : 124;
    lv_obj_t* tagline = lv_label_create(g_screen);
    lv_label_set_text(tagline, "DIGITAL TELEMETRY & RACING CLUSTER");
    lv_obj_set_style_text_color(tagline, lv_color_hex(0x9CA3AF), 0);
    lv_obj_set_style_text_font(tagline, &lv_font_montserrat_12, 0);
    lv_obj_align(tagline, LV_ALIGN_CENTER, 0, tag_y);
    lv_obj_clear_flag(tagline, LV_OBJ_FLAG_CLICKABLE);

    // =========================================================================
    // 5. PILL BADGES DIAGNOSTICA HARDWARE (MicroSD, CAN, 60 FPS)
    // =========================================================================
    const int pill_y = is_portrait ? 158 : 152;
    lv_obj_t* pill_row = lv_obj_create(g_screen);
    lv_obj_remove_style_all(pill_row);
    lv_obj_set_size(pill_row, LV_SIZE_CONTENT, 30);
    lv_obj_align(pill_row, LV_ALIGN_CENTER, 0, pill_y);
    lv_obj_set_flex_flow(pill_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_gap(pill_row, 10, 0);
    lv_obj_clear_flag(pill_row, LV_OBJ_FLAG_CLICKABLE);

    auto make_pill = [&](const char* text, uint32_t bg_col, uint32_t border_col, uint32_t text_col) {
        lv_obj_t* p = lv_obj_create(pill_row);
        lv_obj_remove_style_all(p);
        lv_obj_set_size(p, LV_SIZE_CONTENT, 26);
        lv_obj_set_style_radius(p, 6, 0);
        lv_obj_set_style_bg_color(p, lv_color_hex(bg_col), 0);
        lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(p, lv_color_hex(border_col), 0);
        lv_obj_set_style_border_width(p, 1, 0);
        lv_obj_set_style_pad_hor(p, 10, 0);
        lv_obj_set_style_pad_ver(p, 3, 0);
        lv_obj_clear_flag(p, LV_OBJ_FLAG_CLICKABLE);

        lv_obj_t* l = lv_label_create(p);
        lv_label_set_text(l, text);
        lv_obj_set_style_text_color(l, lv_color_hex(text_col), 0);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_12, 0);
        lv_obj_center(l);
        lv_obj_clear_flag(l, LV_OBJ_FLAG_CLICKABLE);
        return p;
    };

    // 1. Badge MicroSD Card
    if (abarth::storage::sd().isMounted()) {
        char sdbuf[64];
        std::snprintf(sdbuf, sizeof(sdbuf), LV_SYMBOL_SAVE " MicroSD %lluMB (S:/)",
                     static_cast<unsigned long long>(abarth::storage::sd().getTotalSizeMB()));
        make_pill(sdbuf, 0x06200F, 0x16A34A, 0x4ADE80);
    } else {
        make_pill(LV_SYMBOL_SAVE " MicroSD Non Rilevata", 0x1A1A24, 0x333344, 0x888899);
    }

    // 2. Badge Real-Time Clock (RTC)
    if (abarth::storage::rtc().isAvailable()) {
        char rtcbuf[48];
        std::snprintf(rtcbuf, sizeof(rtcbuf), LV_SYMBOL_IMAGE " RTC %s", abarth::storage::rtc().getFormattedClock().c_str());
        make_pill(rtcbuf, 0x1A1438, 0x8B5CF6, 0xC4B5FD);
    } else {
        make_pill(LV_SYMBOL_IMAGE " RTC Non Rilevato", 0x1A1A24, 0x333344, 0x888899);
    }

    // 3. Badge CAN-Bus & Diagnostica
    make_pill(LV_SYMBOL_SETTINGS " BUS CAN / OBD-II", 0x0A1926, 0x0284C7, 0x38BDF8);

    // 4. Badge Motore Grafico ESP32-P4
    make_pill(LV_SYMBOL_GPS " 60 FPS P4-RISC-V", 0x221306, 0xD97706, 0xFBBF24);

    // =========================================================================
    // 6. FRASE CELEBRE CARLO ABARTH / ANEDDOTO MOTORE
    // =========================================================================
    const int quote_y = is_portrait ? 198 : 186;
    lv_obj_t* quote_lbl = lv_label_create(g_screen);
    lv_label_set_text(quote_lbl, random_quote);
    lv_obj_set_style_text_color(quote_lbl, lv_color_hex(0xF59E0B), 0);
    lv_obj_set_style_text_font(quote_lbl, &lv_font_montserrat_12, 0);
    lv_label_set_long_mode(quote_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(quote_lbl, is_portrait ? 500 : 760);
    lv_obj_set_style_text_align(quote_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(quote_lbl, LV_ALIGN_CENTER, 0, quote_y);
    lv_obj_clear_flag(quote_lbl, LV_OBJ_FLAG_CLICKABLE);

    // =========================================================================
    // 7. BLOCCO INFO BUILD E SISTEMA (SE DEBUG MODE E' ATTIVA)
    // =========================================================================
    if (abarth::settings::current().debug_mode) {
        lv_obj_t* info_l = lv_label_create(g_screen);
        lv_label_set_text_fmt(
            info_l,
            "Firmware: abarth-dashboard\n"
            "Build: %s %s\n"
            "LVGL: %d.%d.%d",
            __DATE__, __TIME__,
            LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);
        lv_obj_set_style_text_color(info_l, lv_color_hex(0x6A6A74), 0);
        lv_obj_set_style_text_font(info_l, &lv_font_montserrat_12, 0);
        lv_obj_align(info_l, LV_ALIGN_TOP_LEFT, 20, 20);
        lv_obj_clear_flag(info_l, LV_OBJ_FLAG_CLICKABLE);

        lv_obj_t* info_r = lv_label_create(g_screen);
        const size_t heap_kb  = esp_get_free_heap_size() / 1024;
        const size_t psram_kb = heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024;
        lv_label_set_text_fmt(
            info_r,
            "Hardware: Guition JC1060P470C\n"
            "SoC: ESP32-P4 Dual-Core RISC-V\n"
            "Heap: %u KB | PSRAM: %u KB",
            static_cast<unsigned>(heap_kb),
            static_cast<unsigned>(psram_kb));
        lv_obj_set_style_text_color(info_r, lv_color_hex(0x6A6A74), 0);
        lv_obj_set_style_text_font(info_r, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_align(info_r, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_align(info_r, LV_ALIGN_TOP_RIGHT, -20, 20);
        lv_obj_clear_flag(info_r, LV_OBJ_FLAG_CLICKABLE);
    }

    // =========================================================================
    // 8. PROGRESS BAR NEON & STATO SISTEMA
    // =========================================================================
    const int bar_w = is_portrait ? 460 : 560;

    lv_obj_t* bar_cont = lv_obj_create(g_screen);
    lv_obj_remove_style_all(bar_cont);
    lv_obj_set_width(bar_cont, bar_w);
    lv_obj_set_height(bar_cont, LV_SIZE_CONTENT);
    lv_obj_align(bar_cont, LV_ALIGN_BOTTOM_MID, 0, is_portrait ? -64 : -56);
    lv_obj_set_flex_flow(bar_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(bar_cont, 5, 0);
    lv_obj_clear_flag(bar_cont, LV_OBJ_FLAG_CLICKABLE);

    // Riga Stato + Percentuale
    lv_obj_t* st_row = lv_obj_create(bar_cont);
    lv_obj_remove_style_all(st_row);
    lv_obj_set_width(st_row, LV_PCT(100));
    lv_obj_set_height(st_row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(st_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(st_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(st_row, LV_OBJ_FLAG_CLICKABLE);

    g_status_label = lv_label_create(st_row);
    lv_label_set_text(g_status_label, "Inizializzazione sottosistemi di bordo...");
    lv_obj_set_style_text_color(g_status_label, lv_color_hex(0xF3F4F6), 0);
    lv_obj_set_style_text_font(g_status_label, &lv_font_montserrat_14, 0);
    lv_obj_clear_flag(g_status_label, LV_OBJ_FLAG_CLICKABLE);

    g_pct_label = lv_label_create(st_row);
    lv_label_set_text(g_pct_label, "0%");
    lv_obj_set_style_text_color(g_pct_label, lv_color_hex(0x00FFCC), 0);
    lv_obj_set_style_text_font(g_pct_label, &lv_font_montserrat_14, 0);
    lv_obj_clear_flag(g_pct_label, LV_OBJ_FLAG_CLICKABLE);

    // Barra di avanzamento slim
    g_bar = lv_bar_create(bar_cont);
    lv_obj_set_size(g_bar, LV_PCT(100), 7);
    lv_obj_set_style_bg_color(g_bar, lv_color_hex(0x181824), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(g_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(g_bar, lv_color_hex(0x282838), LV_PART_MAIN);
    lv_obj_set_style_border_width(g_bar, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(g_bar, 4, LV_PART_MAIN);
    lv_obj_set_style_bg_color(g_bar, lv_color_hex(0xEF4444), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(g_bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(g_bar, 4, LV_PART_INDICATOR);
    lv_bar_set_range(g_bar, 0, 100);
    lv_bar_set_value(g_bar, 0, LV_ANIM_OFF);
    lv_obj_clear_flag(g_bar, LV_OBJ_FLAG_CLICKABLE);

    // =========================================================================
    // 9. CLAUSOLA LEGALE: ACCETTAZIONE AUTOMATICA TERMINI & CONDIZIONI
    // =========================================================================
    lv_obj_t* legal_notice = lv_label_create(g_screen);
    lv_label_set_text(legal_notice,
        "L'utilizzo di questo sistema implica l'accettazione automatica\n"
        "di tutti i termini, condizioni d'uso e licenza software riservata");
    lv_obj_set_width(legal_notice, 880);
    lv_label_set_long_mode(legal_notice, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(legal_notice, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(legal_notice, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(legal_notice, lv_color_hex(0xD1D5DB), 0);
    lv_obj_align(legal_notice, LV_ALIGN_BOTTOM_MID, 0, is_portrait ? -28 : -20);
    lv_obj_clear_flag(legal_notice, LV_OBJ_FLAG_CLICKABLE);

    // =========================================================================
    // 10. INDICAZIONE TOUCH INTERATTIVO (TAP TO SKIP)
    // =========================================================================
    lv_obj_t* hint_skip = lv_label_create(g_screen);
    lv_label_set_text(hint_skip, "Tocca lo schermo per accedere subito alla telemetria");
    lv_obj_set_style_text_color(hint_skip, lv_color_hex(0x66667A), 0);
    lv_obj_set_style_text_font(hint_skip, &lv_font_montserrat_10, 0);
    lv_obj_align(hint_skip, LV_ALIGN_BOTTOM_MID, 0, is_portrait ? -10 : -4);
    lv_obj_clear_flag(hint_skip, LV_OBJ_FLAG_CLICKABLE);

    // =========================================================================
    // 10. CARICAMENTO SCHERMO E TIMER PROGRESSIVO
    // =========================================================================
    lv_scr_load(g_screen);
    g_progress = 0;
    // Timer a 50ms per un avanzamento fluido sincronizzato con il boot
    g_progress_tmr = lv_timer_create(progress_cb, 50, nullptr);

    lvgl_port_unlock();
}

void setStatus(const char* text) {
    if (!text) return;
    if (!g_status_label) return;
    if (!lvgl_port_lock(-1)) return;
    lv_label_set_text(g_status_label, text);
    lvgl_port_unlock();
}

bool isDismissed() {
    return g_dismiss_requested;
}

void requestDismiss() {
    g_dismiss_requested = true;
    if (g_bar) {
        lv_bar_set_value(g_bar, 100, LV_ANIM_OFF);
    }
    if (g_pct_label) {
        lv_label_set_text(g_pct_label, "100%");
    }
}

void dismiss() {
    g_dismiss_requested = true;
    if (!lvgl_port_lock(-1)) return;
    if (g_progress_tmr) {
        lv_timer_del(g_progress_tmr);
        g_progress_tmr = nullptr;
    }
    if (g_bar) {
        lv_bar_set_value(g_bar, 100, LV_ANIM_OFF);
    }
    if (g_screen) {
        if (lv_scr_act() != g_screen) {
            lv_anim_del_all();
            lv_obj_del(g_screen);
            g_screen = nullptr;
            g_bar = nullptr;
            g_status_label = nullptr;
            g_pct_label = nullptr;
        }
    }
    lvgl_port_unlock();
}

}  // namespace abarth::ui::splash
