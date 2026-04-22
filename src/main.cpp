/**
 * @file main.cpp
 * @brief Entry point firmware dashboard Abarth 595.
 *
 * Flusso:
 *  1) Inizializza seriale e log
 *  2) Inizializza data store (mutex)
 *  3) Carica le impostazioni utente da NVS (luminosita', unita', ...)
 *  4) Inizializza display (LVGL) applicando la luminosita' salvata
 *  5) Mostra splash "ABARTH 595"
 *  6) Avvia task OBD (BLE → ELM327 → polling)
 *  7) Costruisce la UI principale e nasconde lo splash
 *  8) Loop Arduino vuoto: LVGL gira nel suo task dedicato.
 */

#include <Arduino.h>
#include <esp_log.h>

#include "config/settings.h"
#include "data/data_store.h"
#include "display/board.h"
#include "obd/obd_manager.h"
#include "ui/splash.h"
#include "ui/ui.h"

static constexpr const char* TAG = "main";

void setup() {
    Serial.begin(115200);
    delay(200);
    ESP_LOGI(TAG, "*** Abarth Dashboard booting ***");

    if (!abarth::data::store().init()) {
        ESP_LOGE(TAG, "data store init fallito");
    }

    abarth::settings::init();

    if (!abarth::display::init()) {
        ESP_LOGE(TAG, "display init fallito");
    }

    // Applica la luminosita' salvata (display::init parte a 80% come default).
    abarth::display::setBrightness(
        static_cast<int>(abarth::settings::current().brightness));

    abarth::ui::splash::show();

    abarth::ui::splash::setStatus("Avvio modulo OBD...");
    if (!abarth::obd::obd().start()) {
        ESP_LOGE(TAG, "impossibile avviare task OBD");
    }

    // Lasciamo qualche centinaio di ms di splash per dare tempo al task OBD
    // di fare un primo giro di scansione BLE, poi passiamo alla UI reale.
    delay(2000);

    abarth::ui::splash::setStatus("Caricamento dashboard...");
    abarth::ui::build();
    abarth::ui::splash::dismiss();

    ESP_LOGI(TAG, "setup completato");
}

void loop() {
    // LVGL gira in un suo task interno (configurato dal port).
    // Il task OBD gira su core 1.
    delay(1000);
}
