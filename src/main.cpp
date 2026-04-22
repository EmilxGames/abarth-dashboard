/**
 * @file main.cpp
 * @brief Entry point firmware dashboard Abarth 595.
 *
 * Flusso:
 *  1) Inizializza seriale e log
 *  2) Inizializza data store (mutex)
 *  3) Inizializza display (LVGL)
 *  4) Costruisce UI
 *  5) Avvia task OBD (scansiona BLE, connette dongle, inizializza ELM327,
 *     inizia il polling ciclico dei PID -> scrive in data store)
 *  6) Loop Arduino vuoto: LVGL gira nel suo task dedicato.
 */

#include <Arduino.h>
#include <esp_log.h>

#include "data/data_store.h"
#include "display/board.h"
#include "obd/obd_manager.h"
#include "ui/ui.h"

static constexpr const char* TAG = "main";

void setup() {
    Serial.begin(115200);
    delay(200);
    ESP_LOGI(TAG, "*** Abarth Dashboard booting ***");

    if (!abarth::data::store().init()) {
        ESP_LOGE(TAG, "data store init fallito");
    }

    if (!abarth::display::init()) {
        ESP_LOGE(TAG, "display init fallito");
    }

    abarth::ui::build();

    if (!abarth::obd::obd().start()) {
        ESP_LOGE(TAG, "impossibile avviare task OBD");
    }

    ESP_LOGI(TAG, "setup completato");
}

void loop() {
    // LVGL gira in un suo task interno (configurato dal port).
    // Il task OBD gira su core 1.
    delay(1000);
}
