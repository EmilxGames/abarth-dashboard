#pragma once

/**
 * @file settings.h
 * @brief Impostazioni utente persistenti (NVS / Preferences).
 *
 * Thread-safety: le API leggono/scrivono NVS tramite `Preferences`, che
 * e' tread-safe lato IDF. L'istanza globale fa caching in RAM per letture
 * veloci; le scritture vengono propagate immediatamente in NVS.
 */

#include <cstdint>

namespace abarth::settings {

/// Unita' di misura per le temperature mostrate in UI.
enum class TempUnit : uint8_t {
    Celsius = 0,
    Fahrenheit = 1,
};

/// Unita' di misura per la velocita'.
enum class SpeedUnit : uint8_t {
    KmH = 0,
    Mph = 1,
};

struct Settings {
    /// Luminosita' retroilluminazione in percentuale 10..100.
    uint8_t   brightness   = 80;
    /// Unita' temperatura preferita.
    TempUnit  temp_unit    = TempUnit::Celsius;
    /// Unita' velocita' preferita.
    SpeedUnit speed_unit   = SpeedUnit::KmH;
    /// Tema scuro attivo (unico tema implementato: parametro riservato
    /// per estensioni future).
    bool      dark_theme   = true;
};

/// Inizializza lo store NVS e legge i valori esistenti. Chiamare in main()
/// prima del display::init() per poter applicare subito la luminosita'.
bool init();

/// Restituisce lo snapshot corrente delle impostazioni.
const Settings& current();

/// Aggiorna la luminosita' e la persiste. Applica subito al backlight.
void setBrightness(uint8_t percent);

/// Aggiorna l'unita' di misura temperatura.
void setTempUnit(TempUnit u);

/// Aggiorna l'unita' di misura velocita'.
void setSpeedUnit(SpeedUnit u);

/// Converti un valore Celsius nell'unita' attuale.
float tempInCurrentUnit(float c);
/// Stringa dell'unita' temperatura attuale (es. "\u00b0C" o "\u00b0F").
const char* tempUnitSymbol();

/// Converti un valore km/h nell'unita' attuale.
float speedInCurrentUnit(float kmh);
/// Stringa dell'unita' velocita' attuale.
const char* speedUnitSymbol();

}  // namespace abarth::settings
