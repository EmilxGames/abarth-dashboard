#pragma once

/**
 * @file settings_tab.h
 * @brief Costruzione della tab "Impostazioni".
 *
 * La tab contiene:
 *   - slider luminosita' backlight (persistente in NVS)
 *   - selezione unita' di misura temperatura (C / F)
 *   - selezione unita' di misura velocita' (km/h / mph)
 *   - impostazione data/ora (rollers + "Imposta")
 *   - blocco info sistema (heap, PSRAM, uptime, versione firmware)
 *   - bottone "Test touch" (apre modal con dot che segue il dito)
 *   - bottone "Riavvia dispositivo"
 */

struct _lv_obj_t;
typedef struct _lv_obj_t lv_obj_t;

namespace abarth::ui::settings_tab {

/// Costruisce i widget della tab passata come genitore. La tab deve essere
/// vuota (appena creata da `lv_tabview_add_tab`).
void build(lv_obj_t* tab);

/// Aggiorna i campi che si rinfrescano (info sistema, orologio, stato touch).
/// Va chiamato dal timer UI generale.
void tick();

}  // namespace abarth::ui::settings_tab
