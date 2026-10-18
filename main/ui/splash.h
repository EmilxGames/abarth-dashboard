#pragma once

/**
 * @file splash.h
 * @brief Schermata di boot con logo ABARTH 595 e barra di caricamento.
 *
 * Viene mostrata appena il display e' pronto, prima di costruire la UI
 * principale. La barra di caricamento e' solo estetica: e' animata con
 * un timer LVGL per dare feedback mentre i sottosistemi (BLE, OBD) si
 * inizializzano in background.
 */

#include <cstdint>

namespace abarth::ui::splash {

/// Costruisce e mostra la schermata di splash. Usa il mutex LVGL internamente.
void show();

/// Aggiorna il testo di stato sotto la barra di caricamento (es.
/// "Avvio BLE...", "Connessione OBD..."). Thread-safe.
void setStatus(const char* text);

/// Nasconde lo splash e libera le risorse. Chiamare prima di `ui::build()`.
void dismiss();

/// Restituisce true se lo splash e' gia' stato nascosto o l'utente ha richiesto di saltarlo.
bool isDismissed();

/// Richiede la chiusura anticipata dello splash (es. al tocco dello schermo).
void requestDismiss();

}  // namespace abarth::ui::splash
