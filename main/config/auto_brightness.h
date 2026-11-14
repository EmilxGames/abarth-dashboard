#pragma once
/**
 * @file auto_brightness.h
 * @brief Luminosita' automatica senza sensore di luce: in base all'ora
 *        (orologio RTC) con alba/tramonto calcolati dalla posizione, oppure con
 *        orari fissi. Passaggio graduale (~45 s) tra luminosita' di giorno e di
 *        notte. Senza ora valida resta la luminosita' manuale. Vale per
 *        entrambe le interfacce.
 */
#include <cstdint>

namespace abarth::brightness {

struct Status {
    bool auto_on = false;      ///< modalita' automatica attiva e ora valida
    bool time_valid = false;   ///< l'orologio ha un'ora valida
    bool is_day = true;
    uint8_t current = 0;       ///< luminosita' applicata adesso (%)
    int16_t sunrise_min = -1, sunset_min = -1;   ///< oggi (alba/tramonto), minuti locali
};

/// Avvia il controllo (task leggero, un aggiornamento al secondo); da chiamare a UI pronta
void start();
/// Stato attuale (per le Impostazioni)
Status status();
/// Da chiamare dopo un cambio di impostazioni: applica subito (senza sfumare)
void refreshNow();

}  // namespace abarth::brightness
