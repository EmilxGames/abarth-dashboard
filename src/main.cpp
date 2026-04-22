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
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_system.h>
#include <esp_timer.h>

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
    // Motivo dell'ultimo reset: utile per diagnosticare loop di reboot.
    ESP_LOGI(TAG, "reset reason: %d", static_cast<int>(esp_reset_reason()));

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

    // Diamo qualche istante allo splash per partire e al task OBD per fare
    // il primo tentativo di scan BLE.
    delay(1200);
    abarth::ui::splash::setStatus("Costruzione interfaccia...");

    // Costruzione UI principale: prende il lock LVGL, crea tutti i widget e
    // carica lo screen nuovo (tipicamente 200-400 ms).
    abarth::ui::build();

    abarth::ui::splash::setStatus("Pronto.");

    // Attesa finale per far godere lo splash prima del tabview principale.
    // Totale splash visibile: ~4 s, coerente con la barra di progresso.
    delay(2200);
    abarth::ui::splash::dismiss();

    ESP_LOGI(TAG, "setup completato");
}

void loop() {
    // Heartbeat di diagnostica: stampa heap/psram/uptime ogni 10 s in modo
    // che da seriale si possa capire se il board e' stabile o riparte.
    static uint32_t last_log_s = 0;
    const uint64_t uptime_s = esp_timer_get_time() / 1000000ULL;
    if (uptime_s - last_log_s >= 10) {
        last_log_s = static_cast<uint32_t>(uptime_s);
        const size_t free_heap  = esp_get_free_heap_size();
        const size_t min_heap   = esp_get_minimum_free_heap_size();
        const size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        ESP_LOGI(TAG,
                 "heartbeat uptime=%llus heap=%u (min %u) psram=%u",
                 uptime_s,
                 static_cast<unsigned>(free_heap),
                 static_cast<unsigned>(min_heap),
                 static_cast<unsigned>(free_psram));
    }
    delay(1000);
}
