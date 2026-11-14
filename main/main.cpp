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
#include "config/connection.h"
#include "config/settings.h"
#include "config/warmup_manager.h"
#include "data/data_store.h"
#include "display/board.h"
#include "obd/obd_manager.h"
#include "can/can_manager.h"
#include "storage/sd_card_manager.h"
#include "storage/rtc_manager.h"
#include "storage/ota_updater.h"
#include "storage/web_server.h"
#include "storage/telemetry_logger.h"
#include "ui/splash.h"
#include "ui/ui.h"
#include "ui_v2/ui_v2.h"

#include <esp_ota_ops.h>
#include <driver/gpio.h>

static constexpr const char* TAG = "main";

#define DIAG(fmt, ...) do { printf("[I][main] [DIAG] " fmt "\n", ##__VA_ARGS__); fflush(stdout); } while (0)

// Heartbeat diagnostico: visibile solo con livello di log DEBUG
// (idf.py menuconfig -> Log output, oppure esp_log_level_set("main", ESP_LOG_DEBUG)).
static void heartbeat_task(void* /*arg*/) {
    uint32_t tick = 0;
    while (true) {
        ESP_LOGD(TAG, "[BEAT] #%lu uptime=%llums heap=%uK psram=%uK",
                 static_cast<unsigned long>(tick++),
                 static_cast<unsigned long long>(esp_timer_get_time() / 1000ULL),
                 static_cast<unsigned>(esp_get_free_heap_size() / 1024),
                 static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

extern "C" void app_main(void) {
    // Tieni il pannello LCD (JD9165) in reset per tutta la prima fase di avvio
    // (~3 s di esp_hosted). Dopo un reset software (USB, OTA, panic) il pannello
    // resta alimentato e a volte non riparte con il solo impulso di 10 ms del
    // driver: un reset lungo lo avvicina a una vera riaccensione.
    // GPIO5 = LCD_RST (attivo basso), poi gestito di nuovo dal driver del pannello.
    {
        gpio_config_t io = {};
        io.pin_bit_mask = 1ULL << GPIO_NUM_5;
        io.mode = GPIO_MODE_OUTPUT;
        gpio_config(&io);
        gpio_set_level(GPIO_NUM_5, 0);
    }

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
    {
        // Conta gli avvii consecutivi finiti con crash o watchdog, in qualunque
        // punto. Dopo 2 di fila si torna a impostazioni sicure (UI Classica,
        // OBD Bluetooth): una scelta salvata non puo' bloccare il display per sempre.
        const esp_reset_reason_t rr = esp_reset_reason();
        const bool crashed = rr == ESP_RST_PANIC || rr == ESP_RST_INT_WDT || rr == ESP_RST_TASK_WDT || rr == ESP_RST_WDT;
        const uint8_t fails = crashed ? abarth::settings::bootFailCount() + 1 : 0;
        abarth::settings::setBootFailCount(fails);
        if (fails >= 2) {
            DIAG("%u avvii falliti di fila: ripristino impostazioni sicure", fails);
            abarth::settings::setUiStyle(abarth::settings::UiStyle::Classic);
            abarth::settings::setUiV2Pending(false);
            abarth::settings::setConnectionMode(abarth::settings::ConnectionMode::BleObd);
            abarth::settings::setCanListenOnly(true);
            abarth::settings::setBootFailCount(0);
        }
    }
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
    abarth::storage::telemetry::start();

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
    if (abarth::settings::current().connection_mode == abarth::settings::ConnectionMode::DirectCan) {
        DIAG("step: CAN-Bus start (TWAI ISOCAN)");
        if (!abarth::can::can().start()) {
            DIAG("ERRORE CAN start");
        }
    } else {
        DIAG("step: OBD start");
        if (!abarth::obd::obd().start()) {
            DIAG("ERRORE OBD start");
        }
    }
#else
    DIAG("step: OBD/CAN start SKIPPED (ABARTH_DIAG_SKIP_OBD)");
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
    if (abarth::settings::current().connection_mode == abarth::settings::ConnectionMode::DirectCan) {
        abarth::ui::splash::setStatus("Inizializzazione CAN-Bus Diretto (TWAI)...");
    } else {
        abarth::ui::splash::setStatus("Inizializzazione Bluetooth OBD-II...");
    }
    wait_splash(1800);
    abarth::ui::splash::setStatus("Caricamento telemetria Abarth 595...");
    wait_splash(1800);
    abarth::ui::splash::setStatus("Pronto al via!");
    wait_splash(1000);

#ifndef ABARTH_DIAG_SKIP_UI
    // Scelta interfaccia: Classica o v2 Racing, con ritorno automatico alla
    // Classica se la v2 si e' piantata nei primi 10 s dell'avvio precedente.
    bool use_v2 = abarth::settings::current().ui_style == abarth::settings::UiStyle::Racing;
    if (use_v2 && abarth::settings::isUiV2Pending()) {
        DIAG("UI v2 non confermata all'avvio precedente: torno alla UI Classica");
        abarth::settings::setUiStyle(abarth::settings::UiStyle::Classic);
        abarth::settings::setUiV2Pending(false);
        use_v2 = false;
    }
    if (use_v2) {
        DIAG("step: ui_v2::build (Racing)");
        abarth::settings::setUiV2Pending(true);
        abarth::ui_v2::build();
        static esp_timer_handle_t s_v2_ok_timer = nullptr;
        esp_timer_create_args_t ta = {};
        ta.callback = [](void*) {
            abarth::settings::setUiV2Pending(false);
            abarth::settings::setBootFailCount(0);
            ESP_LOGI(TAG, "UI v2 confermata stabile");
        };
        ta.name = "ui_v2_ok";
        if (esp_timer_create(&ta, &s_v2_ok_timer) == ESP_OK) {
            esp_timer_start_once(s_v2_ok_timer, 10ULL * 1000 * 1000);
        }
        DIAG("ui_v2::build OK");
    } else {
        DIAG("step: ui::build");
        abarth::ui::build();
        DIAG("ui::build OK");
    }
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

    // UI costruita e sorgente dati (CAN o OBD) avviata: il firmware e' sano.
    // Con il rollback attivo, se un aggiornamento OTA non arriva fin qui (crash
    // o reset prima), al riavvio successivo il bootloader torna alla versione
    // precedente.
    {
        const esp_err_t rb = esp_ota_mark_app_valid_cancel_rollback();
        DIAG("firmware confermato valido (rollback annullato): %s", esp_err_to_name(rb));
    }

    // Rete Wi-Fi salvata per internet/web server (non in modalita' OBD Wi-Fi)
    abarth::connection::autoConnectInternetWifi();

    // Avvio completo: azzera il contatore (la UI v2 lo azzera anche dopo 10 s)
    abarth::settings::setBootFailCount(0);
    DIAG("*** app_main completato ***");
    // app_main returns, tasks continue running in background.
}
