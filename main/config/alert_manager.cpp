#include "config/alert_manager.h"
#include "ui_v2/alert_banner.h"

#include <cstdio>
#include <cstring>
#include <esp_log.h>
#include <esp_timer.h>
#include <nvs_flash.h>
#include <lvgl.h>

#include "display/lvgl_v8_port.h"
#include "data/data_store.h"
#include "storage/sd_card_manager.h"

namespace abarth::config {

namespace {
constexpr const char* TAG = "alert_mgr";
constexpr const char* kNvsNamespace = "abarth_alerts";

nvs_handle_t g_nvs_handle = 0;
lv_obj_t* g_alert_modal = nullptr;
lv_timer_t* g_auto_close_timer = nullptr;
uint32_t g_remaining_ticks = 0;
uint32_t g_total_ticks = 0;
lv_obj_t* g_bar_indicator = nullptr;
lv_obj_t* g_timer_lbl = nullptr;

/// Contenitore NON modale per gli avvisi (revisione M7): banner in alto,
/// trasparente e non cliccabile, cosi' i gauge restano visibili e usabili.
/// La card dell'avviso dentro resta toccabile per chiuderla.
lv_obj_t* make_alert_banner_host() {
    lv_obj_t* host = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(host);
    lv_obj_set_width(host, LV_PCT(100));
    lv_obj_set_height(host, LV_SIZE_CONTENT);
    lv_obj_align(host, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_opa(host, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_top(host, 8, 0);
    lv_obj_set_flex_flow(host, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(host, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(host, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(host, LV_OBJ_FLAG_SCROLLABLE);
    return host;
}

void close_alert_popup() {
    if (!lvgl_port_lock(-1)) return;
    if (g_auto_close_timer) {
        lv_timer_del(g_auto_close_timer);
        g_auto_close_timer = nullptr;
    }
    if (g_alert_modal) {
        lv_obj_del(g_alert_modal);
        g_alert_modal = nullptr;
        g_bar_indicator = nullptr;
        g_timer_lbl = nullptr;
    }
    lvgl_port_unlock();
}

void auto_close_timer_cb(lv_timer_t* /*tmr*/) {
    if (g_remaining_ticks > 0) {
        g_remaining_ticks--;
        if (g_bar_indicator && g_total_ticks > 0) {
            int val = (g_remaining_ticks * 100) / g_total_ticks;
            lv_bar_set_value(g_bar_indicator, val, LV_ANIM_OFF);
        }
        if (g_timer_lbl) {
            char t_buf[16];
            int sec = (g_remaining_ticks * 100) / 1000;
            std::snprintf(t_buf, sizeof(t_buf), "(%ds)", sec + 1);
            lv_label_set_text(g_timer_lbl, t_buf);
        }
    } else {
        close_alert_popup();
    }
}

// --- Banner nello stile Moderno (UI v2) --------------------------------------
// Se la UI Moderno e' attiva costruisce il banner nuovo nell'host gia' creato,
// avvia il conto alla rovescia, rilascia il lock e ritorna true.
bool v2_alert(const std::string& title, const std::string& msg, abarth::ui_v2::AlertLevel lvl,
              uint8_t auto_close_sec, const char* ok_label) {
    if (!abarth::ui_v2::alertBannerActive()) return false;
    abarth::ui_v2::buildAlertBanner(g_alert_modal, title.c_str(), msg.c_str(), lvl, ok_label,
                                    auto_close_sec > 0, &g_timer_lbl, [](lv_event_t*) { close_alert_popup(); });
    if (auto_close_sec > 0) {
        g_bar_indicator = nullptr;
        g_remaining_ticks = auto_close_sec * 10;
        g_total_ticks = g_remaining_ticks;
        g_auto_close_timer = lv_timer_create(auto_close_timer_cb, 100, nullptr);
    }
    lvgl_port_unlock();
    return true;
}
} // namespace

AlertManager& AlertManager::instance() {
    static AlertManager inst;
    return inst;
}

AlertManager::AlertManager() {
    // 0: Motore Freddo
    m_rules.push_back({
        AlertType::ColdEngineRpm,
        true,
        "Avviso Motore Freddo (RPM)",
        "Segnala se si superano i giri impostati con liquido refrigerante freddo.",
        2200.0f, // soglia 1: RPM max (es. 2200 RPM)
        45.0f,   // soglia 2: Temp Acqua min (es. 45°C)
        5,       // chiusura automatica dopo 5 secondi
        30,      // cooldown 30 secondi
        0
    });

    // 1: High Coolant Temp
    m_rules.push_back({
        AlertType::HighCoolantTemp,
        true,
        "Allarme Temp. Acqua Elevata",
        "Segnala il superamento della temperatura critica del liquido refrigerante.",
        105.0f, // 105°C
        0.0f,
        0,      // Solo OK manuale (massima sicurezza)
        30,
        0
    });

    // 2: High Oil Temp
    m_rules.push_back({
        AlertType::HighOilTemp,
        true,
        "Allarme Temp. Olio Motore",
        "Segnala una temperatura olio motore elevata.",
        125.0f, // 125°C
        0.0f,
        0,      // Solo OK manuale
        30,
        0
    });

    // 3: High Boost Pressure
    m_rules.push_back({
        AlertType::HighBoostPressure,
        true,
        "Allarme Sovrapressione Turbo",
        "Segnala se la pressione di sovralimentazione supera la soglia di sicurezza.",
        1.65f,  // 1.65 bar
        0.0f,
        5,      // 5 sec
        30,
        0
    });

    // 4: Low Battery Voltage
    m_rules.push_back({
        AlertType::LowBatteryVoltage,
        true,
        "Allarme Tensione Batteria Bassa",
        "Segnala se la tensione della batteria o alternatore scende sotto la soglia.",
        11.5f,  // 11.5 V
        0.0f,
        5,      // 5 sec
        45,
        0
    });
}

void AlertManager::init() {
    esp_err_t err = nvs_open(kNvsNamespace, NVS_READWRITE, &g_nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Impossibile aprire NVS namespace %s: %s", kNvsNamespace, esp_err_to_name(err));
        return;
    }
    loadSettings();
}

void AlertManager::loadSettings() {
    if (!g_nvs_handle) return;
    for (auto& r : m_rules) {
        uint8_t idx = static_cast<uint8_t>(r.type);
        char key_en[16], key_t1[16], key_t2[16], key_ac[16];
        std::snprintf(key_en, sizeof(key_en), "alt_%u_en", idx);
        std::snprintf(key_t1, sizeof(key_t1), "alt_%u_t1", idx);
        std::snprintf(key_t2, sizeof(key_t2), "alt_%u_t2", idx);
        std::snprintf(key_ac, sizeof(key_ac), "alt_%u_ac", idx);

        uint8_t en = r.enabled ? 1 : 0;
        nvs_get_u8(g_nvs_handle, key_en, &en);
        r.enabled = (en != 0);

        uint32_t t1_bits = 0;
        if (nvs_get_u32(g_nvs_handle, key_t1, &t1_bits) == ESP_OK) {
            std::memcpy(&r.threshold1, &t1_bits, sizeof(float));
        }

        uint32_t t2_bits = 0;
        if (nvs_get_u32(g_nvs_handle, key_t2, &t2_bits) == ESP_OK) {
            std::memcpy(&r.threshold2, &t2_bits, sizeof(float));
        }

        uint8_t ac = r.auto_close_sec;
        nvs_get_u8(g_nvs_handle, key_ac, &ac);
        r.auto_close_sec = ac;
    }

    // Carica Custom Rules
    uint8_t num_c = 0;
    nvs_get_u8(g_nvs_handle, "c_num", &num_c);
    m_custom_rules.clear();
    for (uint8_t i = 0; i < num_c; i++) {
        CustomAlertRule cr;
        cr.id = i;
        char k_en[16], k_m[16], k_op[16], k_th[16], k_ac[16], k_nm[16], k_tx[16];
        std::snprintf(k_en, sizeof(k_en), "c_%u_en", i);
        std::snprintf(k_m,  sizeof(k_m),  "c_%u_m", i);
        std::snprintf(k_op, sizeof(k_op), "c_%u_op", i);
        std::snprintf(k_th, sizeof(k_th), "c_%u_th", i);
        std::snprintf(k_ac, sizeof(k_ac), "c_%u_ac", i);
        std::snprintf(k_nm, sizeof(k_nm), "c_%u_nm", i);
        std::snprintf(k_tx, sizeof(k_tx), "c_%u_tx", i);

        uint8_t en = 0; nvs_get_u8(g_nvs_handle, k_en, &en); cr.enabled = (en != 0);
        uint8_t m = 0; nvs_get_u8(g_nvs_handle, k_m, &m); cr.metric = static_cast<abarth::data::Metric>(m);
        uint8_t op = 0; nvs_get_u8(g_nvs_handle, k_op, &op); cr.op = static_cast<AlertOp>(op);

        uint32_t th_bits = 0;
        if (nvs_get_u32(g_nvs_handle, k_th, &th_bits) == ESP_OK) {
            std::memcpy(&cr.threshold, &th_bits, sizeof(float));
        }
        uint8_t ac = 5; nvs_get_u8(g_nvs_handle, k_ac, &ac); cr.auto_close_sec = ac;

        char buf_nm[64] = {0}; size_t len_nm = sizeof(buf_nm);
        if (nvs_get_str(g_nvs_handle, k_nm, buf_nm, &len_nm) == ESP_OK) cr.name = buf_nm;

        char buf_tx[128] = {0}; size_t len_tx = sizeof(buf_tx);
        if (nvs_get_str(g_nvs_handle, k_tx, buf_tx, &len_tx) == ESP_OK) cr.custom_text = buf_tx;

        m_custom_rules.push_back(cr);
    }
    ESP_LOGI(TAG, "Impostazioni alert caricate da NVS.");
}

void AlertManager::saveSettings() {
    if (!g_nvs_handle) return;
    for (const auto& r : m_rules) {
        uint8_t idx = static_cast<uint8_t>(r.type);
        char key_en[16], key_t1[16], key_t2[16], key_ac[16];
        std::snprintf(key_en, sizeof(key_en), "alt_%u_en", idx);
        std::snprintf(key_t1, sizeof(key_t1), "alt_%u_t1", idx);
        std::snprintf(key_t2, sizeof(key_t2), "alt_%u_t2", idx);
        std::snprintf(key_ac, sizeof(key_ac), "alt_%u_ac", idx);

        nvs_set_u8(g_nvs_handle, key_en, r.enabled ? 1 : 0);

        uint32_t t1_bits = 0;
        std::memcpy(&t1_bits, &r.threshold1, sizeof(float));
        nvs_set_u32(g_nvs_handle, key_t1, t1_bits);

        uint32_t t2_bits = 0;
        std::memcpy(&t2_bits, &r.threshold2, sizeof(float));
        nvs_set_u32(g_nvs_handle, key_t2, t2_bits);

        nvs_set_u8(g_nvs_handle, key_ac, r.auto_close_sec);
    }
    nvs_commit(g_nvs_handle);
    saveCustomRules();
}

void AlertManager::saveCustomRules() {
    if (!g_nvs_handle) return;
    uint8_t num_c = static_cast<uint8_t>(m_custom_rules.size());
    nvs_set_u8(g_nvs_handle, "c_num", num_c);

    for (size_t i = 0; i < m_custom_rules.size(); i++) {
        const auto& cr = m_custom_rules[i];
        char k_en[16], k_m[16], k_op[16], k_th[16], k_ac[16], k_nm[16], k_tx[16];
        std::snprintf(k_en, sizeof(k_en), "c_%u_en", (unsigned)i);
        std::snprintf(k_m,  sizeof(k_m),  "c_%u_m", (unsigned)i);
        std::snprintf(k_op, sizeof(k_op), "c_%u_op", (unsigned)i);
        std::snprintf(k_th, sizeof(k_th), "c_%u_th", (unsigned)i);
        std::snprintf(k_ac, sizeof(k_ac), "c_%u_ac", (unsigned)i);
        std::snprintf(k_nm, sizeof(k_nm), "c_%u_nm", (unsigned)i);
        std::snprintf(k_tx, sizeof(k_tx), "c_%u_tx", (unsigned)i);

        nvs_set_u8(g_nvs_handle, k_en, cr.enabled ? 1 : 0);
        nvs_set_u8(g_nvs_handle, k_m, static_cast<uint8_t>(cr.metric));
        nvs_set_u8(g_nvs_handle, k_op, static_cast<uint8_t>(cr.op));

        uint32_t th_bits = 0;
        std::memcpy(&th_bits, &cr.threshold, sizeof(float));
        nvs_set_u32(g_nvs_handle, k_th, th_bits);

        nvs_set_u8(g_nvs_handle, k_ac, cr.auto_close_sec);
        nvs_set_str(g_nvs_handle, k_nm, cr.name.c_str());
        nvs_set_str(g_nvs_handle, k_tx, cr.custom_text.c_str());
    }
    nvs_commit(g_nvs_handle);
}

CustomAlertRule* AlertManager::getCustomRule(uint8_t id) {
    for (auto& cr : m_custom_rules) {
        if (cr.id == id) return &cr;
    }
    return nullptr;
}

CustomAlertRule* AlertManager::addCustomRule() {
    if (m_custom_rules.size() >= 10) return nullptr;
    uint8_t next_id = static_cast<uint8_t>(m_custom_rules.size());
    CustomAlertRule cr;
    cr.id = next_id;
    cr.enabled = true;
    cr.name = "Avviso Personalizzato " + std::to_string(next_id + 1);
    cr.metric = abarth::data::Metric::Rpm;
    cr.op = AlertOp::GreaterThan;
    cr.threshold = 3000.0f;
    cr.custom_text = "Attenzione: Soglia superata!";
    cr.auto_close_sec = 5;
    m_custom_rules.push_back(cr);
    saveCustomRules();
    return &m_custom_rules.back();
}

void AlertManager::removeCustomRule(uint8_t id) {
    for (auto it = m_custom_rules.begin(); it != m_custom_rules.end(); ++it) {
        if (it->id == id) {
            m_custom_rules.erase(it);
            break;
        }
    }
    // Riordina gli ID
    for (size_t i = 0; i < m_custom_rules.size(); i++) {
        m_custom_rules[i].id = static_cast<uint8_t>(i);
    }
    saveCustomRules();
}

AlertRule* AlertManager::getRule(AlertType type) {
    for (auto& r : m_rules) {
        if (r.type == type) return &r;
    }
    return nullptr;
}

void AlertManager::setRuleEnabled(AlertType type, bool enabled) {
    auto* r = getRule(type);
    if (r) {
        r->enabled = enabled;
        saveSettings();
    }
}

void AlertManager::setRuleThreshold1(AlertType type, float val) {
    auto* r = getRule(type);
    if (r) {
        r->threshold1 = val;
        saveSettings();
    }
}

void AlertManager::setRuleThreshold2(AlertType type, float val) {
    auto* r = getRule(type);
    if (r) {
        r->threshold2 = val;
        saveSettings();
    }
}

void AlertManager::setRuleAutoClose(AlertType type, uint8_t sec) {
    auto* r = getRule(type);
    if (r) {
        r->auto_close_sec = sec;
        saveSettings();
    }
}

void AlertManager::checkAlerts() {
    const uint64_t now_ms = esp_timer_get_time() / 1000ULL;
    // Solo valori aggiornati negli ultimi 2 s: se la connessione cade un valore
    // vecchio non deve tenere attivo (o far scattare) un allarme.
    constexpr uint32_t kAlertMaxAgeMs = 2000;
    auto& ds = abarth::data::store();

    const auto s_rpm = ds.get(abarth::data::Metric::Rpm);
    const auto s_coolant = ds.get(abarth::data::Metric::CoolantTempC);
    const auto s_oil_temp = ds.get(abarth::data::Metric::OilTempC);
    const auto s_boost_kpa = ds.get(abarth::data::Metric::BoostKpa);
    const auto s_battery = ds.get(abarth::data::Metric::BatteryVolts);

    for (auto& r : m_rules) {
        if (!r.enabled) continue;

        if (now_ms - r.last_trigger_ms < (static_cast<uint64_t>(r.cooldown_sec) * 1000ULL)) {
            continue;
        }

        bool triggered = false;
        float v1 = 0.0f;
        float v2 = 0.0f;

        switch (r.type) {
            case AlertType::ColdEngineRpm: {
                v1 = s_rpm.value;
                v2 = s_coolant.value;
                if (s_coolant.fresh(kAlertMaxAgeMs, static_cast<int64_t>(now_ms)) && s_rpm.fresh(kAlertMaxAgeMs, static_cast<int64_t>(now_ms))) {
                    if (v2 < r.threshold2 && v1 > r.threshold1) {
                        triggered = true;
                    }
                }
                break;
            }
            case AlertType::HighCoolantTemp: {
                v1 = s_coolant.value;
                if (s_coolant.fresh(kAlertMaxAgeMs, static_cast<int64_t>(now_ms)) && v1 > r.threshold1) {
                    triggered = true;
                }
                break;
            }
            case AlertType::HighOilTemp: {
                v1 = s_oil_temp.value;
                if (s_oil_temp.fresh(kAlertMaxAgeMs, static_cast<int64_t>(now_ms)) && v1 > r.threshold1) {
                    triggered = true;
                }
                break;
            }
            case AlertType::HighBoostPressure: {
                float boost_bar = 0.0f;
                if (s_boost_kpa.value > 101.3f) {
                    boost_bar = (s_boost_kpa.value - 101.3f) / 100.0f;
                }
                v1 = boost_bar;
                if (s_boost_kpa.fresh(kAlertMaxAgeMs, static_cast<int64_t>(now_ms)) && v1 > r.threshold1) {
                    triggered = true;
                }
                break;
            }
            case AlertType::LowBatteryVoltage: {
                v1 = s_battery.value;
                if (s_battery.fresh(kAlertMaxAgeMs, static_cast<int64_t>(now_ms)) && v1 > 5.0f && v1 < r.threshold1) {
                    triggered = true;
                }
                break;
            }
            default: break;
        }

        if (triggered) {
            r.last_trigger_ms = now_ms;
            triggerAlertPopup(r, v1, v2, false);
            return;
        }
    }

    // Valutazione Regole Personalizzate
    for (auto& cr : m_custom_rules) {
        if (!cr.enabled) continue;
        if (now_ms - cr.last_trigger_ms < (static_cast<uint64_t>(cr.cooldown_sec) * 1000ULL)) {
            continue;
        }

        auto s = ds.get(cr.metric);
        if (!s.fresh(kAlertMaxAgeMs, static_cast<int64_t>(now_ms))) continue;

        bool trig = false;
        if (cr.op == AlertOp::GreaterThan && s.value > cr.threshold) trig = true;
        else if (cr.op == AlertOp::LessThan && s.value < cr.threshold) trig = true;
        else if (cr.op == AlertOp::Equal && std::abs(s.value - cr.threshold) < 0.1f) trig = true;

        if (trig) {
            cr.last_trigger_ms = now_ms;
            triggerCustomAlertPopup(cr, s.value, false);
            return;
        }
    }
}

void AlertManager::triggerAlertPopup(const AlertRule& rule, float val1, float val2, bool is_test) {
    if (!lvgl_port_lock(-1)) return;

    // Se esiste gia' un allarme aperto, chiudilo per mostrare quello nuovo
    if (g_alert_modal) {
        close_alert_popup();
    }

    /* Backdrop oscurato sul top layer LVGL */
    g_alert_modal = make_alert_banner_host();
    {
        using abarth::ui_v2::AlertLevel;
        char m[200];
        AlertLevel lv = AlertLevel::Critical;
        switch (rule.type) {
            case AlertType::ColdEngineRpm:
                std::snprintf(m, sizeof(m), "%.0f giri con acqua a %.0f \xC2\xB0" "C. Tieni basso il regime finch\xC3\xA9 il motore non \xC3\xA8 in temperatura.", val1, val2);
                lv = AlertLevel::Warning; break;
            case AlertType::HighCoolantTemp:
                std::snprintf(m, sizeof(m), "Acqua a %.0f \xC2\xB0" "C (soglia %.0f \xC2\xB0" "C). Rallenta e controlla il raffreddamento.", val1, rule.threshold1); break;
            case AlertType::HighOilTemp:
                std::snprintf(m, sizeof(m), "Olio a %.0f \xC2\xB0" "C (soglia %.0f \xC2\xB0" "C). Fai raffreddare il motore a basso regime.", val1, rule.threshold1); break;
            case AlertType::HighBoostPressure:
                std::snprintf(m, sizeof(m), "Turbo a %.2f bar (soglia %.2f bar).", val1, rule.threshold1); break;
            case AlertType::LowBatteryVoltage:
                std::snprintf(m, sizeof(m), "Batteria a %.1f V (minimo %.1f V).", val1, rule.threshold1);
                lv = AlertLevel::Warning; break;
            default:
                std::snprintf(m, sizeof(m), "%s", rule.description.c_str());
        }
        if (v2_alert((is_test ? "[Prova] " : "") + rule.name, m, lv, rule.auto_close_sec, "OK")) return;
    }

    const bool is_portrait = (LV_HOR_RES < LV_VER_RES);
    const int popup_w = is_portrait ? 520 : 560;

    /* Card Finestra Pop-up Glassmorphic scura con bordo rosso racing */
    lv_obj_t* card = lv_obj_create(g_alert_modal);
    lv_obj_remove_style_all(card);
    lv_obj_set_width(card, popup_w);
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x180808), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_90, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0xFF2222), 0);
    lv_obj_set_style_border_width(card, 3, 0);
    lv_obj_set_style_radius(card, 16, 0);
    lv_obj_set_style_pad_all(card, 20, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(card, 12, 0);

    /* Intestazione Pop-up con Icona Warning */
    lv_obj_t* header = lv_obj_create(card);
    lv_obj_remove_style_all(header);
    lv_obj_set_width(header, LV_PCT(100));
    lv_obj_set_height(header, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* ico = lv_label_create(header);
    lv_label_set_text(ico, LV_SYMBOL_WARNING);
    lv_obj_set_style_text_color(ico, lv_color_hex(0xFF2222), 0);
    lv_obj_set_style_text_font(ico, &lv_font_montserrat_28, 0);
    lv_obj_set_style_pad_right(ico, 12, 0);

    lv_obj_t* title = lv_label_create(header);
    std::string title_text = is_test ? "[TEST PREVIEW] " + rule.name : rule.name;
    lv_label_set_text(title, title_text.c_str());
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);
    lv_obj_set_flex_grow(title, 1);

    /* Corpo del Messaggio di Avviso */
    char msg_buf[256];
    if (rule.type == AlertType::ColdEngineRpm) {
        std::snprintf(msg_buf, sizeof(msg_buf),
                      "ATTENZIONE: Motore Freddo!\n\n"
                      "Rilevati %.0f RPM con Temperatura Acqua a %.0f C (soglia freddo: %.0f C, max RPM a freddo: %.0f RPM).\n\n"
                      "Mantieni basso il regime motore fino al raggiungimento della temperatura d'esercizio!",
                      val1, val2, rule.threshold2, rule.threshold1);
    } else if (rule.type == AlertType::HighCoolantTemp) {
        std::snprintf(msg_buf, sizeof(msg_buf),
                      "ALLARME CRITICO: Temperatura Liquido Refrigerante Elevata!\n\n"
                      "Rilevati %.1f C (soglia allarme: %.0f C).\n\n"
                      "Riduci immediatamente la velocita' e controlla il circuito di raffreddamento!",
                      val1, rule.threshold1);
    } else if (rule.type == AlertType::HighOilTemp) {
        std::snprintf(msg_buf, sizeof(msg_buf),
                      "ALLARME: Temperatura Olio Motore Elevata!\n\n"
                      "Rilevati %.1f C (soglia allarme: %.0f C).\n\n"
                      "Fai raffreddare il motore a basso regime!",
                      val1, rule.threshold1);
    } else if (rule.type == AlertType::HighBoostPressure) {
        std::snprintf(msg_buf, sizeof(msg_buf),
                      "ATTENZIONE: Picco Sovrapressione Turbo Elevato!\n\n"
                      "Rilevati %.2f bar (soglia massima: %.2f bar)!",
                      val1, rule.threshold1);
    } else if (rule.type == AlertType::LowBatteryVoltage) {
        std::snprintf(msg_buf, sizeof(msg_buf),
                      "ATTENZIONE: Tensione Batteria Bassa!\n\n"
                      "Rilevati %.1f V (soglia minima: %.1f V).",
                      val1, rule.threshold1);
    } else {
        std::snprintf(msg_buf, sizeof(msg_buf), "%s", rule.description.c_str());
    }

    lv_obj_t* msg_lbl = lv_label_create(card);
    lv_label_set_text(msg_lbl, msg_buf);
    lv_obj_set_style_text_color(msg_lbl, lv_color_hex(0xEEEEEE), 0);
    lv_obj_set_style_text_font(msg_lbl, &lv_font_montserrat_14, 0);
    lv_label_set_long_mode(msg_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(msg_lbl, LV_PCT(100));

    /* Barra di progresso chiusura automatica (se attiva) */
    if (rule.auto_close_sec > 0) {
        g_bar_indicator = lv_bar_create(card);
        lv_obj_set_size(g_bar_indicator, LV_PCT(100), 6);
        lv_obj_set_style_bg_color(g_bar_indicator, lv_color_hex(0x331111), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(g_bar_indicator, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_radius(g_bar_indicator, 3, LV_PART_MAIN);
        lv_obj_set_style_bg_color(g_bar_indicator, lv_color_hex(0xFF3333), LV_PART_INDICATOR);
        lv_obj_set_style_bg_opa(g_bar_indicator, LV_OPA_COVER, LV_PART_INDICATOR);
        lv_obj_set_style_radius(g_bar_indicator, 3, LV_PART_INDICATOR);
        lv_bar_set_range(g_bar_indicator, 0, 100);
        lv_bar_set_value(g_bar_indicator, 100, LV_ANIM_OFF);

        g_remaining_ticks = rule.auto_close_sec * 10; // 100ms per tick
        g_total_ticks = g_remaining_ticks;
        g_auto_close_timer = lv_timer_create(auto_close_timer_cb, 100, nullptr);
    }

    /* Pulsante di Chiusura / Conferma OK */
    lv_obj_t* btn_row = lv_obj_create(card);
    lv_obj_remove_style_all(btn_row);
    lv_obj_set_width(btn_row, LV_PCT(100));
    lv_obj_set_height(btn_row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* btn_ok = lv_btn_create(btn_row);
    lv_obj_set_size(btn_ok, 200, 52);
    lv_obj_set_style_bg_color(btn_ok, lv_color_hex(0xCC0000), 0);
    lv_obj_set_style_radius(btn_ok, 12, 0);

    lv_obj_t* ok_cont = lv_obj_create(btn_ok);
    lv_obj_remove_style_all(ok_cont);
    lv_obj_set_size(ok_cont, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(ok_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ok_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(ok_cont, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(ok_cont, LV_OBJ_FLAG_EVENT_BUBBLE);

    lv_obj_t* l_ok = lv_label_create(ok_cont);
    lv_label_set_text(l_ok, "HO CAPITO");
    lv_obj_set_style_text_color(l_ok, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(l_ok, &lv_font_montserrat_16, 0);
    lv_obj_clear_flag(l_ok, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(l_ok, LV_OBJ_FLAG_EVENT_BUBBLE);

    if (rule.auto_close_sec > 0) {
        g_timer_lbl = lv_label_create(ok_cont);
        char t_buf[16];
        std::snprintf(t_buf, sizeof(t_buf), "(%ds)", rule.auto_close_sec);
        lv_label_set_text(g_timer_lbl, t_buf);
        lv_obj_set_style_text_color(g_timer_lbl, lv_color_hex(0xFFCCCC), 0);
        lv_obj_set_style_text_font(g_timer_lbl, &lv_font_montserrat_16, 0);
        lv_obj_set_style_pad_left(g_timer_lbl, 6, 0);
        lv_obj_clear_flag(g_timer_lbl, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(g_timer_lbl, LV_OBJ_FLAG_EVENT_BUBBLE);
    }

    auto on_ok_clicked = [](lv_event_t* /*e*/){
        close_alert_popup();
    };

    lv_obj_add_event_cb(btn_ok, on_ok_clicked, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(ok_cont, on_ok_clicked, LV_EVENT_CLICKED, nullptr);

    lvgl_port_unlock();
}

void AlertManager::triggerCustomAlertPopup(const CustomAlertRule& rule, float val, bool is_test) {
    if (!lvgl_port_lock(-1)) return;

    if (g_alert_modal) {
        close_alert_popup();
    }

    g_alert_modal = make_alert_banner_host();
    {
        char m[200];
        std::snprintf(m, sizeof(m), "%s\nValore %.1f \xC2\xB7 soglia %.1f", rule.custom_text.c_str(), val, rule.threshold);
        if (v2_alert((is_test ? "[Prova] " : "") + rule.name, m, abarth::ui_v2::AlertLevel::Warning, rule.auto_close_sec, "OK")) return;
    }

    const bool is_portrait = (LV_HOR_RES < LV_VER_RES);
    const int popup_w = is_portrait ? 520 : 560;

    lv_obj_t* card = lv_obj_create(g_alert_modal);
    lv_obj_remove_style_all(card);
    lv_obj_set_width(card, popup_w);
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x180808), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_90, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0xFF8800), 0); // Bordo arancione per avvisi personalizzati
    lv_obj_set_style_border_width(card, 3, 0);
    lv_obj_set_style_radius(card, 16, 0);
    lv_obj_set_style_pad_all(card, 20, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(card, 12, 0);

    lv_obj_t* header = lv_obj_create(card);
    lv_obj_remove_style_all(header);
    lv_obj_set_width(header, LV_PCT(100));
    lv_obj_set_height(header, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(header, 10, 0);

    lv_obj_t* icon = lv_label_create(header);
    lv_label_set_text(icon, LV_SYMBOL_WARNING);
    lv_obj_set_style_text_color(icon, lv_color_hex(0xF59E0B), 0);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_22, 0);

    lv_obj_t* title = lv_label_create(header);
    std::string title_text = is_test ? "[TEST PREVIEW] " + rule.name : rule.name;
    lv_label_set_text(title, title_text.c_str());
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);
    lv_obj_set_flex_grow(title, 1);

    char msg_buf[256];
    std::snprintf(msg_buf, sizeof(msg_buf), "%s\n\n(Valore attuale rilevato: %.1f, Soglia impostata: %.1f)",
                  rule.custom_text.c_str(), val, rule.threshold);

    lv_obj_t* msg_lbl = lv_label_create(card);
    lv_label_set_text(msg_lbl, msg_buf);
    lv_obj_set_style_text_color(msg_lbl, lv_color_hex(0xEEEEEE), 0);
    lv_obj_set_style_text_font(msg_lbl, &lv_font_montserrat_14, 0);
    lv_label_set_long_mode(msg_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(msg_lbl, LV_PCT(100));

    if (rule.auto_close_sec > 0) {
        g_bar_indicator = lv_bar_create(card);
        lv_obj_set_size(g_bar_indicator, LV_PCT(100), 6);
        lv_obj_set_style_bg_color(g_bar_indicator, lv_color_hex(0x332211), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(g_bar_indicator, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_radius(g_bar_indicator, 3, LV_PART_MAIN);
        lv_obj_set_style_bg_color(g_bar_indicator, lv_color_hex(0xFF8800), LV_PART_INDICATOR);
        lv_obj_set_style_bg_opa(g_bar_indicator, LV_OPA_COVER, LV_PART_INDICATOR);
        lv_obj_set_style_radius(g_bar_indicator, 3, LV_PART_INDICATOR);
        lv_bar_set_range(g_bar_indicator, 0, 100);
        lv_bar_set_value(g_bar_indicator, 100, LV_ANIM_OFF);

        g_remaining_ticks = rule.auto_close_sec * 10;
        g_total_ticks = g_remaining_ticks;
        g_auto_close_timer = lv_timer_create(auto_close_timer_cb, 100, nullptr);
    }

    lv_obj_t* btn_row = lv_obj_create(card);
    lv_obj_remove_style_all(btn_row);
    lv_obj_set_width(btn_row, LV_PCT(100));
    lv_obj_set_height(btn_row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* btn_ok = lv_btn_create(btn_row);
    lv_obj_set_size(btn_ok, 200, 52);
    lv_obj_set_style_bg_color(btn_ok, lv_color_hex(0xCC5500), 0);
    lv_obj_set_style_radius(btn_ok, 12, 0);

    lv_obj_t* ok_cont = lv_obj_create(btn_ok);
    lv_obj_remove_style_all(ok_cont);
    lv_obj_set_size(ok_cont, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(ok_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ok_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(ok_cont, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(ok_cont, LV_OBJ_FLAG_EVENT_BUBBLE);

    lv_obj_t* l_ok = lv_label_create(ok_cont);
    lv_label_set_text(l_ok, "HO CAPITO");
    lv_obj_set_style_text_color(l_ok, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(l_ok, &lv_font_montserrat_16, 0);
    lv_obj_clear_flag(l_ok, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(l_ok, LV_OBJ_FLAG_EVENT_BUBBLE);

    if (rule.auto_close_sec > 0) {
        g_timer_lbl = lv_label_create(ok_cont);
        char t_buf[16];
        std::snprintf(t_buf, sizeof(t_buf), "(%ds)", rule.auto_close_sec);
        lv_label_set_text(g_timer_lbl, t_buf);
        lv_obj_set_style_text_color(g_timer_lbl, lv_color_hex(0xFFEECC), 0);
        lv_obj_set_style_text_font(g_timer_lbl, &lv_font_montserrat_16, 0);
        lv_obj_set_style_pad_left(g_timer_lbl, 6, 0);
        lv_obj_clear_flag(g_timer_lbl, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(g_timer_lbl, LV_OBJ_FLAG_EVENT_BUBBLE);
    }

    auto on_ok_clicked = [](lv_event_t* /*e*/){
        close_alert_popup();
    };

    lv_obj_add_event_cb(btn_ok, on_ok_clicked, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(ok_cont, on_ok_clicked, LV_EVENT_CLICKED, nullptr);

    lvgl_port_unlock();
}

void AlertManager::closeCurrentAlert() {
    close_alert_popup();
}

void AlertManager::triggerColdEngineWarmupAlert(float rpm, float water_temp, uint16_t remaining_sec, bool is_warming, bool is_test) {
    if (!lvgl_port_lock(-1)) return;

    if (g_alert_modal) {
        close_alert_popup();
    }

    g_alert_modal = make_alert_banner_host();
    {
        char m[200];
        if (is_warming) {
            std::snprintf(m, sizeof(m), "%.0f giri con l'olio ancora freddo. Mancano circa %02d:%02d al riscaldamento: evita il pieno carico.",
                          rpm, remaining_sec / 60, remaining_sec % 60);
        } else {
            std::snprintf(m, sizeof(m), "%.0f giri con acqua a %.0f \xC2\xB0" "C: olio ancora denso, tieni basso il regime.", rpm, water_temp);
        }
        if (v2_alert(is_test ? "[Prova] Motore freddo" : "Motore freddo", m, abarth::ui_v2::AlertLevel::Warning, 6, "OK")) return;
    }

    const bool is_portrait = (LV_HOR_RES < LV_VER_RES);
    const int popup_w = is_portrait ? 520 : 560;

    lv_obj_t* card = lv_obj_create(g_alert_modal);
    lv_obj_remove_style_all(card);
    lv_obj_set_width(card, popup_w);
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x180A0A), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_90, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0xFF3333), 0);
    lv_obj_set_style_border_width(card, 3, 0);
    lv_obj_set_style_radius(card, 16, 0);
    lv_obj_set_style_pad_all(card, 20, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(card, 12, 0);

    lv_obj_t* header = lv_obj_create(card);
    lv_obj_remove_style_all(header);
    lv_obj_set_width(header, LV_PCT(100));
    lv_obj_set_height(header, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    if (abarth::storage::sd().isMounted()) {
        lv_obj_t* img = lv_image_create(header);
        lv_image_set_src(img, "S:/icons/warning.png");
        lv_obj_set_style_pad_right(img, 12, 0);
    } else {
        lv_obj_t* ico = lv_label_create(header);
        lv_label_set_text(ico, LV_SYMBOL_WARNING);
        lv_obj_set_style_text_color(ico, lv_color_hex(0xFF3333), 0);
        lv_obj_set_style_text_font(ico, &lv_font_montserrat_28, 0);
        lv_obj_set_style_pad_right(ico, 12, 0);
    }

    lv_obj_t* title = lv_label_create(header);
    lv_label_set_text(title, is_test ? "[TEST] ATTENZIONE: MOTORE FREDDO!" : "ATTENZIONE: MOTORE FREDDO!");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);
    lv_obj_set_flex_grow(title, 1);

    char msg_buf[256];
    if (is_warming) {
        int m = remaining_sec / 60;
        int s = remaining_sec % 60;
        std::snprintf(msg_buf, sizeof(msg_buf),
                      "Rilevati %.0f RPM con riscaldamento olio ancora in corso!\n\n"
                      "Mancano circa %02d:%02d al completamento del warmup.\n\n"
                      "Evita accelerazioni a pieno carico finche' l'olio non e' a regime!",
                      rpm, m, s);
    } else {
        std::snprintf(msg_buf, sizeof(msg_buf),
                      "Rilevati %.0f RPM con Liquido Refrigerante a %.0f C.\n\n"
                      "L'olio motore e' ancora freddo e denso!\n\n"
                      "Mantieni il regime basso per preservare turbocompressore, bronzine e fasce elastiche.",
                      rpm, water_temp);
    }

    lv_obj_t* msg_lbl = lv_label_create(card);
    lv_label_set_text(msg_lbl, msg_buf);
    lv_obj_set_style_text_color(msg_lbl, lv_color_hex(0xEEEEEE), 0);
    lv_obj_set_style_text_font(msg_lbl, &lv_font_montserrat_14, 0);
    lv_label_set_long_mode(msg_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(msg_lbl, LV_PCT(100));

    const uint8_t auto_close_sec = 6;
    g_bar_indicator = lv_bar_create(card);
    lv_obj_set_size(g_bar_indicator, LV_PCT(100), 6);
    lv_obj_set_style_bg_color(g_bar_indicator, lv_color_hex(0x331111), LV_PART_MAIN);
    lv_obj_set_style_radius(g_bar_indicator, 3, LV_PART_MAIN);
    lv_obj_set_style_bg_color(g_bar_indicator, lv_color_hex(0xFF3333), LV_PART_INDICATOR);
    lv_obj_set_style_radius(g_bar_indicator, 3, LV_PART_INDICATOR);
    lv_bar_set_range(g_bar_indicator, 0, 100);
    lv_bar_set_value(g_bar_indicator, 100, LV_ANIM_OFF);

    g_remaining_ticks = auto_close_sec * 10;
    g_total_ticks = g_remaining_ticks;
    g_auto_close_timer = lv_timer_create(auto_close_timer_cb, 100, nullptr);

    lv_obj_t* btn_row = lv_obj_create(card);
    lv_obj_remove_style_all(btn_row);
    lv_obj_set_width(btn_row, LV_PCT(100));
    lv_obj_set_height(btn_row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* btn_ok = lv_btn_create(btn_row);
    lv_obj_set_size(btn_ok, 200, 50);
    lv_obj_set_style_bg_color(btn_ok, lv_color_hex(0xCC0000), 0);
    lv_obj_set_style_radius(btn_ok, 12, 0);

    lv_obj_t* ok_cont = lv_obj_create(btn_ok);
    lv_obj_remove_style_all(ok_cont);
    lv_obj_set_size(ok_cont, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(ok_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ok_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(ok_cont, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(ok_cont, LV_OBJ_FLAG_EVENT_BUBBLE);

    lv_obj_t* l_ok = lv_label_create(ok_cont);
    lv_label_set_text(l_ok, "HO CAPITO");
    lv_obj_set_style_text_color(l_ok, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(l_ok, &lv_font_montserrat_16, 0);

    g_timer_lbl = lv_label_create(ok_cont);
    char t_buf[16];
    std::snprintf(t_buf, sizeof(t_buf), "(%ds)", auto_close_sec);
    lv_label_set_text(g_timer_lbl, t_buf);
    lv_obj_set_style_text_color(g_timer_lbl, lv_color_hex(0xFFCCCC), 0);
    lv_obj_set_style_text_font(g_timer_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_pad_left(g_timer_lbl, 6, 0);

    auto on_ok = [](lv_event_t*){ close_alert_popup(); };
    lv_obj_add_event_cb(btn_ok, on_ok, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(ok_cont, on_ok, LV_EVENT_CLICKED, nullptr);

    lvgl_port_unlock();
}

void AlertManager::triggerWarmupCompleteAlert(bool is_test) {
    if (!lvgl_port_lock(-1)) return;

    if (g_alert_modal) {
        close_alert_popup();
    }

    g_alert_modal = make_alert_banner_host();
    if (v2_alert(is_test ? "[Prova] Olio in temperatura" : "Olio in temperatura",
                 "Puoi usare tutto il motore. Buona guida.", abarth::ui_v2::AlertLevel::Info, 5, "OK")) return;

    const bool is_portrait = (LV_HOR_RES < LV_VER_RES);
    const int popup_w = is_portrait ? 520 : 560;

    lv_obj_t* card = lv_obj_create(g_alert_modal);
    lv_obj_remove_style_all(card);
    lv_obj_set_width(card, popup_w);
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x0A180E), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_90, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x22C55E), 0);
    lv_obj_set_style_border_width(card, 3, 0);
    lv_obj_set_style_radius(card, 16, 0);
    lv_obj_set_style_pad_all(card, 20, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(card, 12, 0);

    lv_obj_t* header = lv_obj_create(card);
    lv_obj_remove_style_all(header);
    lv_obj_set_width(header, LV_PCT(100));
    lv_obj_set_height(header, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    if (abarth::storage::sd().isMounted()) {
        lv_obj_t* img = lv_image_create(header);
        lv_image_set_src(img, "S:/icons/checkered_flag.png");
        lv_obj_set_style_pad_right(img, 12, 0);
    } else {
        lv_obj_t* ico = lv_label_create(header);
        lv_label_set_text(ico, LV_SYMBOL_OK);
        lv_obj_set_style_text_color(ico, lv_color_hex(0x22C55E), 0);
        lv_obj_set_style_text_font(ico, &lv_font_montserrat_28, 0);
        lv_obj_set_style_pad_right(ico, 12, 0);
    }

    lv_obj_t* title = lv_label_create(header);
    lv_label_set_text(title, is_test ? "[TEST] MOTORE IN TEMPERATURA!" : "MOTORE IN TEMPERATURA!");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);
    lv_obj_set_flex_grow(title, 1);

    lv_obj_t* msg_lbl = lv_label_create(card);
    lv_label_set_text(msg_lbl,
        "Liquido refrigerante e olio motore hanno raggiunto la temperatura ideale di esercizio!\n\n"
        "La viscosita' dell'olio e' ottimale e il motore e' pronto per le massime prestazioni.\n\n"
        "Puoi iniziare a tirare in totale sicurezza!\n"
        "Buona guida con la tua Abarth!");
    lv_obj_set_style_text_color(msg_lbl, lv_color_hex(0xEEEEEE), 0);
    lv_obj_set_style_text_font(msg_lbl, &lv_font_montserrat_14, 0);
    lv_label_set_long_mode(msg_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(msg_lbl, LV_PCT(100));

    const uint8_t auto_close_sec = 7;
    g_bar_indicator = lv_bar_create(card);
    lv_obj_set_size(g_bar_indicator, LV_PCT(100), 6);
    lv_obj_set_style_bg_color(g_bar_indicator, lv_color_hex(0x0E2614), LV_PART_MAIN);
    lv_obj_set_style_radius(g_bar_indicator, 3, LV_PART_MAIN);
    lv_obj_set_style_bg_color(g_bar_indicator, lv_color_hex(0x22C55E), LV_PART_INDICATOR);
    lv_obj_set_style_radius(g_bar_indicator, 3, LV_PART_INDICATOR);
    lv_bar_set_range(g_bar_indicator, 0, 100);
    lv_bar_set_value(g_bar_indicator, 100, LV_ANIM_OFF);

    g_remaining_ticks = auto_close_sec * 10;
    g_total_ticks = g_remaining_ticks;
    g_auto_close_timer = lv_timer_create(auto_close_timer_cb, 100, nullptr);

    lv_obj_t* btn_row = lv_obj_create(card);
    lv_obj_remove_style_all(btn_row);
    lv_obj_set_width(btn_row, LV_PCT(100));
    lv_obj_set_height(btn_row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* btn_ok = lv_btn_create(btn_row);
    lv_obj_set_size(btn_ok, 200, 50);
    lv_obj_set_style_bg_color(btn_ok, lv_color_hex(0x16A34A), 0);
    lv_obj_set_style_radius(btn_ok, 12, 0);

    lv_obj_t* ok_cont = lv_obj_create(btn_ok);
    lv_obj_remove_style_all(ok_cont);
    lv_obj_set_size(ok_cont, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(ok_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ok_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(ok_cont, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(ok_cont, LV_OBJ_FLAG_EVENT_BUBBLE);

    lv_obj_t* l_ok = lv_label_create(ok_cont);
    lv_label_set_text(l_ok, "OTTIMO!");
    lv_obj_set_style_text_color(l_ok, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(l_ok, &lv_font_montserrat_16, 0);

    g_timer_lbl = lv_label_create(ok_cont);
    char t_buf[16];
    std::snprintf(t_buf, sizeof(t_buf), "(%ds)", auto_close_sec);
    lv_label_set_text(g_timer_lbl, t_buf);
    lv_obj_set_style_text_color(g_timer_lbl, lv_color_hex(0xDCFCE7), 0);
    lv_obj_set_style_text_font(g_timer_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_pad_left(g_timer_lbl, 6, 0);

    auto on_ok = [](lv_event_t*){ close_alert_popup(); };
    lv_obj_add_event_cb(btn_ok, on_ok, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(ok_cont, on_ok, LV_EVENT_CLICKED, nullptr);

    lvgl_port_unlock();
}

} // namespace abarth::config
