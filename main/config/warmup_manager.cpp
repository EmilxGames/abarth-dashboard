#include "config/warmup_manager.h"

#include <esp_log.h>
#include <esp_timer.h>
#include <algorithm>

#include "config/alert_manager.h"
#include "config/settings.h"

namespace abarth::warmup {

static const char* TAG = "warmup";

WarmupManager& WarmupManager::instance() {
    static WarmupManager inst;
    return inst;
}

WarmupManager::WarmupManager() {
    const auto& s = settings::current();
    m_remaining_sec = s.oil_warmup_countdown_sec;
    m_estimated_oil_temp = 20.0f;
}

void WarmupManager::init() {
    resetCycle();
    ESP_LOGI(TAG, "WarmupManager inizializzato (countdown=%u sec, temp_h2o=%.0f C, rpm_lim=%.0f)",
             settings::current().oil_warmup_countdown_sec,
             settings::current().oil_warmup_water_temp,
             settings::current().oil_cold_rpm_limit);
}

void WarmupManager::resetCycle() {
    const auto& s = settings::current();
    m_state = WarmupState::Cold;
    m_remaining_sec = s.oil_warmup_countdown_sec;
    m_ready_alert_shown = false;
    m_last_tick_ms = esp_timer_get_time() / 1000ULL;
    m_water_peak = 0.0f;
}

float WarmupManager::progressRatio() const {
    if (m_state == WarmupState::Ready) return 1.0f;
    if (m_state == WarmupState::Cold) return 0.0f;
    const auto& s = settings::current();
    if (s.oil_warmup_countdown_sec == 0) return 1.0f;
    float done = static_cast<float>(s.oil_warmup_countdown_sec - m_remaining_sec);
    float ratio = done / static_cast<float>(s.oil_warmup_countdown_sec);
    if (ratio < 0.0f) ratio = 0.0f;
    if (ratio > 1.0f) ratio = 1.0f;
    return ratio;
}

void WarmupManager::update() {
    const auto& s = settings::current();
    if (!s.oil_warmup_enabled) {
        m_state = WarmupState::Disabled;
        return;
    }

    const uint64_t now_ms = esp_timer_get_time() / 1000ULL;
    if (m_last_tick_ms == 0) {
        m_last_tick_ms = now_ms;
        return;
    }

    uint32_t elapsed_sec = static_cast<uint32_t>((now_ms - m_last_tick_ms) / 1000ULL);

    auto& ds = data::store();
    const auto s_coolant = ds.get(data::Metric::CoolantTempC);
    const auto s_rpm     = ds.get(data::Metric::Rpm);

    const float water_temp = s_coolant.valid() ? s_coolant.value : (s.demo_mode ? 88.0f : 0.0f);
    const float current_rpm = s_rpm.valid() ? s_rpm.value : 0.0f;

    if (water_temp > m_water_peak) {
        m_water_peak = water_temp;
    }

    // Se il motore era Ready ma l'acqua scende sotto i 55°C (auto spenta per lungo tempo), resetta
    if (m_state == WarmupState::Ready && water_temp > 0.0f && water_temp < 55.0f) {
        resetCycle();
    }

    switch (m_state) {
        case WarmupState::Disabled:
            m_state = WarmupState::Cold;
            m_remaining_sec = s.oil_warmup_countdown_sec;
            break;

        case WarmupState::Cold: {
            // Se l'acqua raggiunge o supera la soglia impostata (default 80°C), avvia il countdown
            if (water_temp >= s.oil_warmup_water_temp) {
                m_state = WarmupState::Warming;
                m_remaining_sec = s.oil_warmup_countdown_sec;
                m_last_tick_ms = now_ms;
                elapsed_sec = 0;
            } else {
                // Calcola temperatura stimata prima del raggiungimento soglia acqua
                // Parte da ~20°C e sale progressivamente fino a ~56°C
                m_estimated_oil_temp = std::max(20.0f, water_temp * 0.70f);

                // Controllo sovraregime a freddo (con cooldown 30s)
                if (current_rpm > s.oil_cold_rpm_limit && (now_ms - m_last_cold_alert_ms > 30000ULL)) {
                    m_last_cold_alert_ms = now_ms;
                    triggerColdAlert(current_rpm, water_temp, false);
                }
            }
            break;
        }

        case WarmupState::Warming: {
            if (elapsed_sec > 0) {
                m_last_tick_ms += (static_cast<uint64_t>(elapsed_sec) * 1000ULL);
                if (m_remaining_sec > elapsed_sec) {
                    m_remaining_sec -= elapsed_sec;
                } else {
                    m_remaining_sec = 0;
                }
            }

            // Calcola temperatura olio stimata in fase di countdown
            // Sale progressivamente da 56°C a 85°C
            float ratio = progressRatio();
            m_estimated_oil_temp = 56.0f + ratio * (85.0f - 56.0f);

            // Controllo sovraregime durante il countdown (con cooldown 30s)
            if (current_rpm > s.oil_cold_rpm_limit && (now_ms - m_last_cold_alert_ms > 30000ULL)) {
                m_last_cold_alert_ms = now_ms;
                triggerColdAlert(current_rpm, water_temp, false);
            }

            // Countdown terminato!
            if (m_remaining_sec == 0) {
                m_state = WarmupState::Ready;
                m_estimated_oil_temp = 85.0f;
                if (!m_ready_alert_shown) {
                    m_ready_alert_shown = true;
                    triggerReadyAlert(false);
                }
            }
            break;
        }

        case WarmupState::Ready: {
            // A regime: olio stabile tra 85°C e 95°C in base alla temperatura acqua
            m_estimated_oil_temp = 85.0f + std::min(10.0f, std::max(0.0f, (water_temp - 80.0f) * 0.5f));
            break;
        }
    }

    // Se abilitata la stima virtuale in telemetria e il sensore fisico non fornisce dato
    if (s.oil_virtual_temp_display) {
        const auto s_oil = ds.get(data::Metric::OilTempC);
        if (!s_oil.valid() || s.demo_mode) {
            ds.set(data::Metric::OilTempC, m_estimated_oil_temp);
        }
    }
}

void WarmupManager::triggerColdAlert(float current_rpm, float current_water, bool is_test) {
    config::alerts().triggerColdEngineWarmupAlert(current_rpm, current_water, m_remaining_sec, m_state == WarmupState::Warming, is_test);
}

void WarmupManager::triggerReadyAlert(bool is_test) {
    config::alerts().triggerWarmupCompleteAlert(is_test);
}

} // namespace abarth::warmup
