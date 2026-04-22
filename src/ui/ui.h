#pragma once

/**
 * @file ui.h
 * @brief Costruzione dell'interfaccia LVGL.
 *
 * Struttura:
 *   TabView (orizzontale)
 *     ├── Motore       (RPM, velocita', carico, acceleratore)
 *     ├── Temperature  (acqua, olio, aria aspirata, ambiente, EGT)
 *     ├── Pressioni    (boost, MAP, olio)
 *     ├── Elettrico    (tensione batteria, lambda/AFR)
 *     └── Stato        (connessione dongle, contatori, log)
 *
 * L'aggiornamento dei valori avviene tramite un timer LVGL che legge da
 * `abarth::data::store()`. Non si fanno chiamate BLE dal thread UI.
 */

namespace abarth::ui {

/// Costruisce l'intera UI. Da chiamare dopo `display::init()`.
void build();

}  // namespace abarth::ui
