#pragma once

/**
 * @file telemetry_logger.h
 * @brief Registrazione telemetria su MicroSD in un task dedicato (revisione A7).
 *
 * - funziona sia in modalita' CAN diretto sia OBD (registra quando arrivano dati);
 * - il file CSV resta aperto per tutta la sessione, buffer da 4 KB,
 *   fflush + fsync ogni 5 s;
 * - i valori vecchi (> 2 s) vengono scritti vuoti e la colonna age_max_ms
 *   indica l'eta' del dato piu' vecchio tra quelli registrati.
 */
namespace abarth::storage::telemetry {

/// Avvia il task di logging (una sola volta, dopo l'init della SD).
void start();

/// Chiude il file e sospende il logging (da chiamare prima di smontare la SD).
void pause();

/// Riprende il logging (riapre il file alla prossima riga).
void resume();

}  // namespace abarth::storage::telemetry
