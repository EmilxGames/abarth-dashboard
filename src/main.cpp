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

// Diagnostica: usiamo printf() diretto (come fa la libreria esp_panel con
// ESP_UTILS_LOGI) perche' ne' Serial.printf ne' ESP_LOGI finiscono sulla USB
// Serial JTAG del Guition JC1060P470C in questa configurazione.  printf() su
// ESP32-P4 Arduino e' instradato su stdout che il device monitor legge
// (infatti i log [I][Panel] arrivano).
#define DIAG(fmt, ...) do { printf("[I][main] [DIAG] " fmt "\n", ##__VA_ARGS__); fflush(stdout); } while (0)

static void heartbeat_task(void* /*arg*/) {
    // Task separato, pinnato su entrambi i core liberamente.  Se questo
    // heartbeat smette di comparire vuol dire che lo scheduler e' bloccato.
    uint32_t tick = 0;
    while (true) {
        const uint64_t uptime_ms = esp_timer_get_time() / 1000ULL;
        const size_t   heap_kb   = esp_get_free_heap_size() / 1024;
        const size_t   psram_kb  = heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024;
        printf("[I][main] [BEAT] #%lu uptime=%llums heap=%uK psram=%uK\n",
               static_cast<unsigned long>(tick++),
               uptime_ms,
               static_cast<unsigned>(heap_kb),
               static_cast<unsigned>(psram_kb));
        fflush(stdout);
        // 200 ms per pinpoint-are l'esatto momento del crash (HP_WDT fires
        // su interrupts-disabled > 300 ms).
        vTaskDelay(pdMS_TO_TICKS(200));
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

    // ---- Modalita' diagnostica: attiva i bypass con -D nel platformio.ini.
    //      Ogni bypass rimuove un sospettato dalla catena per isolare chi
    //      triggera l'HP_WDT ~4s dopo che la UI principale diventa attiva.
#ifndef ABARTH_DIAG_SKIP_OBD
    DIAG("step: OBD start");
    if (!abarth::obd::obd().start()) {
        DIAG("ERRORE OBD start");
    }
#else
    DIAG("step: OBD start SKIPPED (ABARTH_DIAG_SKIP_OBD)");
#endif

    DIAG("step: splash hold 1200ms");
    delay(1200);
    abarth::ui::splash::setStatus("Costruzione interfaccia...");

#ifndef ABARTH_DIAG_SKIP_UI
    DIAG("step: ui::build");
    abarth::ui::build();
    DIAG("ui::build OK");
#else
    DIAG("step: ui::build SKIPPED (ABARTH_DIAG_SKIP_UI)");
#endif

    abarth::ui::splash::setStatus("Pronto.");

    DIAG("step: splash hold finale 2200ms");
    delay(2200);

#ifndef ABARTH_DIAG_KEEP_SPLASH
    DIAG("step: splash dismiss");
    abarth::ui::splash::dismiss();
#else
    DIAG("step: splash dismiss SKIPPED (ABARTH_DIAG_KEEP_SPLASH)");
#endif

    DIAG("*** setup completato, entro in loop() ***");
}

void loop() {
    // Il loop Arduino fa solo vTaskDelay: il lavoro vero e' nel task LVGL
    // (port esp-arduino-libs) e nel task OBD.
    delay(1000);
}
