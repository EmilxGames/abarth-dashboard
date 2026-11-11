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

/// Applica immediatamente lo stato di visibilità dei manometri e delle schede
/// aggiornando layout, nascondendo/mostrando elementi ed espandendo gli spazi liberi.
void apply_metric_visibility();

/// Ricostruisce dinamicamente i widget del Pannello Personalizzato (Custom Tab)
void rebuild_custom_tab();

}  // namespace abarth::ui
