#pragma once

#include <cstdint>
#include <string>
#include "data/data_store.h"

namespace abarth::warmup {

enum class WarmupState : uint8_t {
    Disabled = 0,
    Cold     = 1, ///< Acqua < soglia impostata (motore freddo)
    Warming  = 2, ///< Acqua >= soglia impostata, countdown attivo (olio in riscaldamento)
    Ready    = 3  ///< Countdown completato, olio e acqua a regime (pronto a tirare!)
};

class WarmupManager {
public:
    static WarmupManager& instance();

    void init();

    /// Chiamata periodicamente (es. da UI tick o loop di sistema)
    void update();

    WarmupState state() const { return m_state; }
    uint16_t remainingSeconds() const { return m_remaining_sec; }
    float estimatedOilTemp() const { return m_estimated_oil_temp; }
    float progressRatio() const; ///< 0.0f (cold) -> 1.0f (ready)

    /// Resetta il ciclo di warmup (permette di ricominciare da Cold/test)
    void resetCycle();

    /// Mostra il popup di allarme motore freddo (o test)
    void triggerColdAlert(float current_rpm, float current_water, bool is_test = false);

    /// Mostra il popup di notifica motore in temperatura (o test)
    void triggerReadyAlert(bool is_test = false);

private:
    WarmupManager();

    WarmupState m_state = WarmupState::Cold;
    uint16_t    m_remaining_sec = 300;
    float       m_estimated_oil_temp = 20.0f;
    uint64_t    m_last_tick_ms = 0;
    uint64_t    m_last_cold_alert_ms = 0;
    bool        m_ready_alert_shown = false;
    float       m_water_peak = 0.0f;
};

inline WarmupManager& warmup_mgr() { return WarmupManager::instance(); }
inline WarmupManager& warmupManager() { return WarmupManager::instance(); }

} // namespace abarth::warmup
