#pragma once
/**
 * @file fuel_log.h
 * @brief Rifornimenti: rilevati dal livello carburante OBD (PID 0x2F, %,
 *        filtrato con una mediana perche' il galleggiante oscilla). Un
 *        rifornimento e' un aumento del livello di almeno la soglia scelta
 *        rispetto all'ultimo livello stabile (anche tra uno spegnimento e la
 *        riaccensione). Litri = differenza di % x capacita' del serbatoio.
 *        Km dalla velocita' OBD integrata nel tempo. Registro su microSD
 *        (/sdcard/rifornimenti*.bin), altrimenti ultimi 10 in NVS. Registro
 *        separato per la simulazione, come per le notifiche.
 */
#include <cstdint>
#include <vector>

namespace abarth::fuel {

constexpr int kMax = 60;

struct Refuel {
    uint32_t epoch = 0;        ///< data e ora (0 = orologio non impostato)
    uint16_t liters_x10 = 0;   ///< litri x10
    uint8_t level_before = 0;  ///< % prima
    uint8_t level_after = 0;   ///< % dopo
    uint32_t price_milli = 0;  ///< euro al litro x1000 (0 = non inserito)
    uint32_t km_x10 = 0;       ///< km percorsi dal rifornimento precedente x10
    uint8_t manual = 0;        ///< aggiunto a mano
    uint8_t pad[3] = {};
};

struct Stats {
    bool has_consumption = false;
    float l_per_100km = 0, km_per_l = 0;
    bool has_cost = false;
    float eur_per_km = 0;
    float month_eur = 0;          ///< spesa del mese corrente
    bool has_range = false;
    float range_km = 0;           ///< autonomia stimata
    float level_pct = -1;         ///< livello attuale filtrato (-1 = non disponibile)
    float km_since_last = 0;      ///< km dall'ultimo rifornimento
};

/// Avvia il rilevamento (task leggero, un passo al secondo); a UI pronta
void start();
/// Registro in uso (simulato se la simulazione e' attiva), dal piu' recente
std::vector<Refuel> list();
Stats stats();
/// Modifica/cancella la voce idx (indice di list())
void update(int idx, const Refuel& r);
void remove(int idx);
/// Rifornimento aggiunto a mano (livello attuale come "dopo"); ritorna l'indice (0)
int addManual(float liters);
/// Ultimo prezzo al litro inserito (x1000)
uint32_t lastPriceMilli();
/// Solo test: livello forzato (%) al posto di quello OBD; < 0 = disattivato
void debugOverrideLevel(float pct);

}  // namespace abarth::fuel
