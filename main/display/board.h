#pragma once

/**
 * @file board.h
 * @brief Inizializzazione display + touch per il Guition JC1060P470C-I-W-Y.
 *
 * Usa la libreria `esp-arduino-libs/ESP32_Display_Panel` che gia' contiene
 * la configurazione preset per questa board (MIPI-DSI JD9365 + touch GT911).
 *
 * In caso di aggiornamenti della libreria che cambiano l'API, la parte
 * da sistemare e' localizzata in `board.cpp`.
 */

namespace abarth::display {

/// Inizializza hardware, back-light e registra i driver con LVGL v9.
/// Ritorna true se la sequenza completa e' andata a buon fine.
bool init();

/// Regola la luminosita' (0..100).
void setBrightness(int percent);

/// Hook da chiamare periodicamente (utile solo se il port non usa task
/// dedicato). Nella config attuale non serve ma viene esposto comunque.
void tick();

}  // namespace abarth::display
