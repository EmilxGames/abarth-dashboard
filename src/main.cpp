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

// Diagnostica: tutti i Serial.printf qui bypassano CORE_DEBUG_LEVEL e si vedono
// sempre sul monitor seriale a 115200.  Utile per isolare i reset HP_SYS_HP_WDT
// quando il device riparte prima che le normali ESP_LOGI vengano mostrate.
#define DIAG(fmt, ...) do { Serial.printf("[DIAG] " fmt "\n", ##__VA_ARGS__); Serial.flush(); } while (0)

static void heartbeat_task(void* /*arg*/) {
    // Task separato, pin su entrambi i core liberamente.  Se questo heartbeat
    // smette di comparire vuol dire che lo scheduler e' bloccato.
    uint32_t tick = 0;
    while (true) {
        const uint64_t uptime_ms = esp_timer_get_time() / 1000ULL;
        const size_t   heap_kb   = esp_get_free_heap_size() / 1024;
        const size_t   psram_kb  = heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024;
        Serial.printf("[BEAT] #%lu uptime=%llums heap=%uK psram=%uK\n",
                      static_cast<unsigned long>(tick++),
                      uptime_ms,
                      static_cast<unsigned>(heap_kb),
                      static_cast<unsigned>(psram_kb));
        Serial.flush();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void setup() {
    Serial.begin(115200);
    delay(200);
    DIAG("*** Abarth Dashboard booting ***");
    DIAG("reset reason = %d", static_cast<int>(esp_reset_reason()));
    DIAG("free heap  = %u B", static_cast<unsigned>(esp_get_free_heap_size()));
    DIAG("free psram = %u B", static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));

    // Avvia subito il task di heartbeat, cosi' se un init dopo blocca
    // scheduler / interrupt vediamo esattamente a che punto si e' fermato.
    xTaskCreatePinnedToCore(heartbeat_task, "beat", 3072, nullptr,
                            tskIDLE_PRIORITY + 1, nullptr, tskNO_AFFINITY);
    DIAG("heartbeat task avviato");

    DIAG("step: data store init");
    if (!abarth::data::store().init()) {
        DIAG("ERRORE data store init");
    }

    DIAG("step: settings init (NVS)");
    abarth::settings::init();

    DIAG("step: display init");
    if (!abarth::display::init()) {
        DIAG("ERRORE display init");
    }
    DIAG("display init OK");

    DIAG("step: setBrightness %d",
         static_cast<int>(abarth::settings::current().brightness));
    abarth::display::setBrightness(
        static_cast<int>(abarth::settings::current().brightness));

    DIAG("step: splash show");
    abarth::ui::splash::show();
    abarth::ui::splash::setStatus("Avvio modulo OBD...");

    DIAG("step: OBD start");
    if (!abarth::obd::obd().start()) {
        DIAG("ERRORE OBD start");
    }

    DIAG("step: splash hold 1200ms");
    delay(1200);
    abarth::ui::splash::setStatus("Costruzione interfaccia...");

    DIAG("step: ui::build");
    abarth::ui::build();
    DIAG("ui::build OK");

    abarth::ui::splash::setStatus("Pronto.");

    DIAG("step: splash hold finale 2200ms");
    delay(2200);

    DIAG("step: splash dismiss");
    abarth::ui::splash::dismiss();

    DIAG("*** setup completato, entro in loop() ***");
}

void loop() {
    // Il loop Arduino fa solo vTaskDelay: il lavoro vero e' nel task LVGL
    // (port esp-arduino-libs) e nel task OBD.
    delay(1000);
}
