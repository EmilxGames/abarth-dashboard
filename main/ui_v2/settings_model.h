#pragma once
/**
 * @file settings_model.h
 * @brief Impostazioni leggere della UI v2 come elenco di righe con valore
 *        corrente e azione, indipendente da come vengono disegnate.
 *        Le azioni riusano le stesse funzioni della UI Classica (settings::,
 *        web::, obd/can) con le stesse protezioni.
 */
#include <cstdint>

namespace abarth::ui_v2::setup {

enum class RowId : uint8_t {
    Brightness,      ///< slider 10-100 %, salvataggio al rilascio
    TempUnit,        ///< C / F
    SpeedUnit,       ///< km/h / mph
    PressureUnit,    ///< bar / kPa / entrambi
    Orientation,     ///< verticale / orizzontale (riavvio)
    Connection,      ///< Bluetooth / WiFi / CAN diretto
    AutoConnect,     ///< on/off
    WebServer,       ///< avvia/arresta (richiede WiFi)
    DemoMode,        ///< simulazione on/off
    UiStyle,         ///< Racing / Classica (riavvio)
    Advanced,        ///< apre le Impostazioni complete
    Count
};

struct RowInfo {
    RowId       id;
    const char* section;   ///< intestazione di gruppo
    const char* title;
};

const RowInfo* rows(int& count);

/// Testo del valore corrente (es. "70 %", "km/h", "Bluetooth")
void valueText(RowId id, char* out, int out_sz);

/// Tocco sulla riga: passa al valore successivo o esegue l'azione.
/// Ritorna un messaggio da mostrare all'utente (nullptr se nessuno).
/// `needs_reboot` = true se il cambio si applica al riavvio.
const char* activate(RowId id, bool& needs_reboot);

/// Luminosita': anteprima durante il trascinamento / salvataggio al rilascio
void brightnessPreview(int pct);
void brightnessCommit(int pct);
int  brightness();

}  // namespace abarth::ui_v2::setup
