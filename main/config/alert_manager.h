#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "data/data_store.h"

namespace abarth::config {

enum class AlertType : uint8_t {
    ColdEngineRpm = 0,     ///< Motore Freddo (Coolant < T_cold && RPM > RPM_max)
    HighCoolantTemp = 1,   ///< Sovratemperatura Liquido Refrigerante (> T_max)
    HighOilTemp = 2,       ///< Sovratemperatura Olio Motore (> T_max)
    HighBoostPressure = 3, ///< Sovrapressione Turbo (> P_max)
    LowBatteryVoltage = 4, ///< Bassa Tensione Batteria (< V_min)
    CustomRule = 10        ///< Allarme/Avviso personalizzato definito dall'utente
};

enum class AlertOp : uint8_t {
    GreaterThan = 0, ///< Valore > Soglia
    LessThan = 1,    ///< Valore < Soglia
    Equal = 2        ///< Valore == Soglia
};

struct CustomAlertRule {
    uint8_t id;             ///< ID univoco della regola personalizzata (0..9)
    bool enabled = false;
    std::string name = "Nuovo Avviso";
    abarth::data::Metric metric = abarth::data::Metric::Rpm;
    AlertOp op = AlertOp::GreaterThan;
    float threshold = 2000.0f;
    std::string custom_text = "Attenzione: Valore limite superato!";
    uint8_t auto_close_sec = 5; // 0 = fisso solo con OK
    uint32_t cooldown_sec = 15;
    uint64_t last_trigger_ms = 0;
};

struct AlertRule {
    AlertType type;
    bool enabled;
    std::string name;
    std::string description;
    
    // Parametri numerici della regola
    float threshold1; // Primary threshold (e.g. 2000 RPM or 105 °C)
    float threshold2; // Secondary threshold if needed (e.g. 50 °C for cold coolant)
    
    uint8_t auto_close_sec; // 0 = Solo pulsante OK, 3, 5, 10, 15 sec per chiusura automatica
    uint32_t cooldown_sec;  // Cooldown in secondi prima di poter scattare di nuovo (es. 30s)
    
    uint64_t last_trigger_ms; // Timestamp ultimo allarme scattato per cooldown
};

class AlertManager {
public:
    static AlertManager& instance();

    void init();
    
    const std::vector<AlertRule>& rules() const { return m_rules; }
    AlertRule* getRule(AlertType type);

    const std::vector<CustomAlertRule>& customRules() const { return m_custom_rules; }
    CustomAlertRule* getCustomRule(uint8_t id);
    CustomAlertRule* addCustomRule();
    void removeCustomRule(uint8_t id);

    void setRuleEnabled(AlertType type, bool enabled);
    void setRuleThreshold1(AlertType type, float val);
    void setRuleThreshold2(AlertType type, float val);
    void setRuleAutoClose(AlertType type, uint8_t sec);

    void saveCustomRules();

    /// Valuta le regole attive in base ai dati correnti dal data store.
    void checkAlerts();

    /// Mostra la finestra pop-up dell'allarme sul layer top di LVGL.
    void triggerAlertPopup(const AlertRule& rule, float val1, float val2 = 0.0f, bool is_test = false);
    void triggerCustomAlertPopup(const CustomAlertRule& rule, float val, bool is_test = false);

    /// Avvisi specifici per Warmup e Protezione Olio
    void triggerColdEngineWarmupAlert(float rpm, float water_temp, uint16_t remaining_sec, bool is_warming, bool is_test = false);
    void triggerWarmupCompleteAlert(bool is_test = false);
    void closeCurrentAlert();

private:
    AlertManager();
    void loadSettings();
    void saveSettings();

    std::vector<AlertRule> m_rules;
    std::vector<CustomAlertRule> m_custom_rules;
};

inline AlertManager& alerts() { return AlertManager::instance(); }

} // namespace abarth::config
