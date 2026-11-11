#include "ui_v2/pages.h"

#include <esp_system.h>
#include "lvgl.h"
#include "config/settings.h"
#include "ui/settings_tab.h"
#include "ui_v2/theme.h"

namespace abarth::ui_v2::pages {

namespace {
using namespace theme;

lv_obj_t* g_setup_page = nullptr;
lv_obj_t* g_adv_host = nullptr;   ///< contenitore delle Impostazioni avanzate (Classiche)
int32_t g_w = 0, g_h = 0;

lv_obj_t* placeholder(lv_obj_t* parent, const char* txt) {
    lv_obj_t* l = lv_label_create(parent);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_color(l, c(kMuted), 0);
    lv_obj_set_style_text_font(l, fontLabel(), 0);
    lv_obj_center(l);
    return l;
}

lv_obj_t* setup_button(lv_obj_t* parent, int32_t y, const char* txt, uint32_t bg, lv_event_cb_t cb, void* ud) {
    lv_obj_t* b = lv_obj_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_set_pos(b, kGap * 2, y);
    lv_obj_set_size(b, g_w - kGap * 4, 64);
    lv_obj_set_style_bg_color(b, c(bg), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, c(kPanelHi), LV_STATE_PRESSED);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t* l = lv_label_create(b);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_color(l, c(kText), 0);
    lv_obj_set_style_text_font(l, fontLabel(), 0);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, kPad, 0);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, ud);
    return b;
}

void style_cb(lv_event_t* e) {
    const auto st = static_cast<settings::UiStyle>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
    if (st == settings::current().ui_style) return;
    settings::setUiStyle(st);
    esp_restart();  // il cambio di interfaccia si applica al riavvio
}

void adv_close_cb(lv_event_t*) {
    if (g_adv_host) lv_obj_add_flag(g_adv_host, LV_OBJ_FLAG_HIDDEN);
}

void adv_open_cb(lv_event_t*) {
    if (!g_adv_host) {
        // Le Impostazioni complete della UI Classica, costruite solo al primo uso
        g_adv_host = lv_obj_create(g_setup_page);
        lv_obj_remove_style_all(g_adv_host);
        lv_obj_set_pos(g_adv_host, 0, 0);
        lv_obj_set_size(g_adv_host, g_w, g_h);
        lv_obj_set_style_bg_color(g_adv_host, c(kBg), 0);
        lv_obj_set_style_bg_opa(g_adv_host, LV_OPA_COVER, 0);
        lv_obj_remove_flag(g_adv_host, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* back = lv_obj_create(g_adv_host);
        lv_obj_remove_style_all(back);
        lv_obj_set_pos(back, 0, 0);
        lv_obj_set_size(back, g_w, 48);
        lv_obj_set_style_bg_color(back, c(kPanel), 0);
        lv_obj_set_style_bg_opa(back, LV_OPA_COVER, 0);
        lv_obj_add_flag(back, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_t* bl = lv_label_create(back);
        lv_label_set_text(bl, LV_SYMBOL_LEFT "  Indietro");
        lv_obj_set_style_text_color(bl, c(kText), 0);
        lv_obj_set_style_text_font(bl, fontLabel(), 0);
        lv_obj_align(bl, LV_ALIGN_LEFT_MID, kPad, 0);
        lv_obj_add_event_cb(back, adv_close_cb, LV_EVENT_CLICKED, nullptr);

        lv_obj_t* host = lv_obj_create(g_adv_host);
        lv_obj_remove_style_all(host);
        lv_obj_set_pos(host, 0, 48);
        lv_obj_set_size(host, g_w, g_h - 48);
        lv_obj_remove_flag(host, LV_OBJ_FLAG_SCROLLABLE);
        ui::settings_tab::build(host);
    }
    lv_obj_remove_flag(g_adv_host, LV_OBJ_FLAG_HIDDEN);
}

void build_setup(lv_obj_t* parent) {
    g_setup_page = parent;
    lv_obj_t* t = lv_label_create(parent);
    lv_label_set_text(t, "STILE INTERFACCIA");
    lv_obj_set_style_text_color(t, c(kMuted), 0);
    lv_obj_set_style_text_font(t, fontSmall(), 0);
    lv_obj_set_pos(t, kGap * 2, 16);

    const bool racing = settings::current().ui_style == settings::UiStyle::Racing;
    setup_button(parent, 44, racing ? "Racing (attiva)" : "Racing", racing ? kRed : kPanel, style_cb,
                 reinterpret_cast<void*>(static_cast<uintptr_t>(settings::UiStyle::Racing)));
    setup_button(parent, 44 + 72, !racing ? "Classica (attiva)" : "Classica  (riavvia il display)", kPanel, style_cb,
                 reinterpret_cast<void*>(static_cast<uintptr_t>(settings::UiStyle::Classic)));

    lv_obj_t* t2 = lv_label_create(parent);
    lv_label_set_text(t2, "TUTTE LE IMPOSTAZIONI");
    lv_obj_set_style_text_color(t2, c(kMuted), 0);
    lv_obj_set_style_text_font(t2, fontSmall(), 0);
    lv_obj_set_pos(t2, kGap * 2, 44 + 72 * 2 + 16);
    setup_button(parent, 44 + 72 * 2 + 44, LV_SYMBOL_SETTINGS "  Impostazioni avanzate", kPanel, adv_open_cb, nullptr);
}
}  // namespace

void build(Page p, lv_obj_t* parent, int32_t w, int32_t h) {
    g_w = w;
    g_h = h;
    switch (p) {
        case Page::Setup:       build_setup(parent); break;
        case Page::Guida:       placeholder(parent, "GUIDA"); break;
        case Page::Temperature: placeholder(parent, "TEMPERATURE"); break;
        case Page::Pressioni:   placeholder(parent, "PRESSIONI"); break;
        case Page::Elettrico:   placeholder(parent, "ELETTRICO / CARBURANTE"); break;
        case Page::Personale:   placeholder(parent, "PERSONALE"); break;
        default: break;
    }
}

void update(Page p, int64_t /*now_ms*/) {
    if (p == Page::Setup && g_adv_host && !lv_obj_has_flag(g_adv_host, LV_OBJ_FLAG_HIDDEN)) {
        static uint32_t n = 0;
        if ((n++ % 2) == 0) ui::settings_tab::tick();  // 10 Hz come nella Classica
    }
}

void onShow(Page /*p*/) {}

}  // namespace abarth::ui_v2::pages
