/**
 * @file main.cpp
 * @brief Entry point firmware dashboard Abarth 595.
 */

#include "nvs_flash.h"
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "esp_hosted.h"

#include "config/alert_manager.h"
#include "config/settings.h"
#include "config/warmup_manager.h"
#include "data/data_store.h"
#include "display/board.h"
#include "obd/obd_manager.h"
#include "storage/sd_card_manager.h"
#include "storage/rtc_manager.h"
#include "storage/ota_updater.h"
#include "storage/web_server.h"
#include "ui/splash.h"
#include "ui/ui.h"

static constexpr const char* TAG = "main";

#define DIAG(fmt, ...) do { printf("[I][main] [DIAG] " fmt "\n", ##__VA_ARGS__); fflush(stdout); } while (0)

static void heartbeat_task(void* /*arg*/) {
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
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

extern "C" void app_main(void) {
    // Inizializza NVS (necessario per WiFi/BLE e impostazioni utente)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    DIAG("step: esp_hosted connect");
    esp_hosted_connect_to_slave();
    if (ESP_OK != esp_hosted_bt_controller_init()) {
        DIAG("ERRORE esp_hosted_bt_controller_init");
    }
    if (ESP_OK != esp_hosted_bt_controller_enable()) {
        DIAG("ERRORE esp_hosted_bt_controller_enable");
    }
    DIAG("esp_hosted BT controller started");

    vTaskDelay(pdMS_TO_TICKS(200));
    DIAG("*** Abarth Dashboard booting ***");
    DIAG("reset reason = %d", static_cast<int>(esp_reset_reason()));
    DIAG("free heap  = %u B", static_cast<unsigned>(esp_get_free_heap_size()));
    DIAG("free psram = %u B", static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));

    xTaskCreatePinnedToCore(heartbeat_task, "beat", 3072, nullptr,
                            tskIDLE_PRIORITY + 1, nullptr, tskNO_AFFINITY);
    DIAG("heartbeat task avviato");

    DIAG("step: data store init");
    if (!abarth::data::store().init()) {
        DIAG("ERRORE data store init");
    }

    DIAG("step: settings init (NVS)");
    abarth::settings::init();
    abarth::config::alerts().init();
    abarth::warmup::warmup_mgr().init();

    DIAG("step: display init");
    if (!abarth::display::init()) {
        DIAG("ERRORE display init");
    }
    DIAG("display init OK");

    DIAG("step: RTC init (Real-Time Clock)");
    abarth::storage::rtc().init();
    if (abarth::storage::rtc().isAvailable()) {
        DIAG("RTC rilevato: %s [0x%02X], Batteria: %s, Ora: %s",
             abarth::storage::rtc().getChipName(),
             abarth::storage::rtc().getI2cAddress(),
             abarth::storage::rtc().isBatteryOk() ? "OK" : "LOW/RESET",
             abarth::storage::rtc().getFormattedTime().c_str());
    } else {
        DIAG("RTC non rilevato (continuazione con clock interno)");
    }

    DIAG("step: SD card init (Slot 0)");
    abarth::storage::sd().init();

    DIAG("step: setBrightness %d",
         static_cast<int>(abarth::settings::current().brightness));
    abarth::display::setBrightness(
        static_cast<int>(abarth::settings::current().brightness));

    DIAG("step: splash show");
    abarth::ui::splash::show();
    if (abarth::storage::sd().isMounted()) {
        char sd_msg[64];
        snprintf(sd_msg, sizeof(sd_msg), "MicroSD OK (%llu MB) - S:/",
                 static_cast<unsigned long long>(abarth::storage::sd().getTotalSizeMB()));
        abarth::ui::splash::setStatus(sd_msg);
    } else {
        abarth::ui::splash::setStatus("Avvio modulo OBD...");
    }

    DIAG("step: check MicroSD firmware update");
    if (abarth::storage::sd().isMounted()) {
        abarth::ota::ota().checkAndApplySdUpdate([](int pct, const char* msg){
            abarth::ui::splash::setStatus(msg);
        });
    }

#ifndef ABARTH_DIAG_SKIP_OBD
    DIAG("step: OBD start");
    if (!abarth::obd::obd().start()) {
        DIAG("ERRORE OBD start");
    }
#else
    DIAG("step: OBD start SKIPPED (ABARTH_DIAG_SKIP_OBD)");
#endif

    auto wait_splash = [](int total_ms) {
        int elapsed = 0;
        while (elapsed < total_ms) {
            if (abarth::ui::splash::isDismissed()) break;
            vTaskDelay(pdMS_TO_TICKS(100));
            elapsed += 100;
        }
    };

    DIAG("step: splash hold stages");
    abarth::ui::splash::setStatus("Inizializzazione Bluetooth OBD-II...");
    wait_splash(1800);
    abarth::ui::splash::setStatus("Caricamento telemetria Abarth 595...");
    wait_splash(1800);
    abarth::ui::splash::setStatus("Pronto al via!");
    wait_splash(1000);

#ifndef ABARTH_DIAG_SKIP_UI
    DIAG("step: ui::build");
    abarth::ui::build();
    DIAG("ui::build OK");
#else
    DIAG("step: ui::build SKIPPED (ABARTH_DIAG_SKIP_UI)");
#endif

    vTaskDelay(pdMS_TO_TICKS(300));

#ifndef ABARTH_DIAG_KEEP_SPLASH
    DIAG("step: splash dismiss");
    abarth::ui::splash::dismiss();
#else
    DIAG("step: splash dismiss SKIPPED (ABARTH_DIAG_KEEP_SPLASH)");
#endif

    DIAG("*** app_main completato ***");
    // app_main returns, tasks continue running in background.
}
