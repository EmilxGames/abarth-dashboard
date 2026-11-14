#pragma once

/**
 * @file data_store.h
 * @brief Contenitore thread-safe per i valori letti dalla centralina.
 *
 * Il task OBD scrive i valori; il task UI legge periodicamente gli snapshot.
 * Usa un mutex FreeRTOS per evitare torn-read su valori 32 bit.
 */

#include <cstdint>
#include <optional>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace abarth::data {

/// Enumera le grandezze rese visibili all'interfaccia.
enum class Metric : uint8_t {
    Rpm = 0,
    Speed,
    EngineLoad,
    Throttle,
    CoolantTempC,
    IntakeAirTempC,
    AmbientTempC,
    OilTempC,
    EgtC,
    BoostKpa,         ///< pressione collettore (MAP) assoluta
    BoostRelBar,      ///< boost relativo rispetto a pressione atmosferica (~1 bar)
    OilPressureBar,
    BatteryVolts,
    Afr,              ///< Lambda * 14.7
    // --- Carburante ---
    FuelLevelPct,        ///< livello serbatoio % (PID 012F)
    FuelPressureKpa,     ///< pressione carburante rail kPa (PID 0123 / 0159)
    FuelRateLh,          ///< portata carburante L/h (PID 015E)
    FuelConsumptionL100, ///< consumo L/100km (calcolato da portata+velocita')
    FuelTempC,           ///< temperatura carburante °C (PID 0126, raro)
    FuelInjectionTiming, ///< angolo iniezione ° (PID 015D, raro)
    EthanolPct,          ///< % etanolo carburante (PID 0152, Flex)
    DistanceSinceClear,  ///< km percorsi da reset DTC (PID 0131)
    Count_            ///< sentinel, non rimuovere
};

/// Valore letto, con timestamp dell'ultimo update (ms da boot, -1 = mai) e
/// flag di supporto PID (false se la centralina ha risposto "NO DATA").
struct Sample {
    float   value      = 0.0f;
    int64_t updated_ms = -1;
    bool    supported  = true;   ///< diventa false se la ECU ha risposto NO DATA
    /// quando e' stata marcata "non supportata" (per riprovarla dopo un po').
    /// NON tocca updated_ms: prima marcare il PID rinfrescava il timestamp e il
    /// vecchio valore tornava "fresco" e ricompariva (numeri a caso a auto spenta).
    int64_t nosupport_ms = -1;

    /// Ritorna true se e' stato aggiornato almeno una volta.
    bool valid() const { return updated_ms >= 0; }

    /// Ritorna true se e' "fresco" (aggiornato negli ultimi N ms).
    bool fresh(uint32_t max_age_ms, int64_t now_ms) const {
        return valid() && (now_ms - updated_ms) <= static_cast<int64_t>(max_age_ms);
    }
};

class DataStore {
public:
    DataStore();
    ~DataStore();

    DataStore(const DataStore&)            = delete;
    DataStore& operator=(const DataStore&) = delete;

    /// Deve essere chiamato prima dell'uso (crea il mutex).
    bool init();

    /// Aggiorna il valore di una metrica e la marca come supportata.
    void set(Metric m, float value);

    /// Ritorna uno snapshot della metrica richiesta.
    Sample get(Metric m) const;

    /// Marca una metrica come "non disponibile" (reset completo).
    void invalidate(Metric m);

    /// Marca una metrica come non supportata dalla centralina (NO DATA).
    void markNotSupported(Metric m);

    /// Resetta il flag "non supportato" su tutte le metriche (es. alla
    /// riconnessione del dongle).
    void clearSupportFlags();

private:
    mutable SemaphoreHandle_t mutex_ = nullptr;
    Sample                    samples_[static_cast<size_t>(Metric::Count_)];
};

/// Accesso globale (singleton semplice, inizializzato in main()).
DataStore& store();

}  // namespace abarth::data
