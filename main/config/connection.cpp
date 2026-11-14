#include "config/connection.h"
#include "can/can_manager.h"
#include "obd/obd_manager.h"
#include "config/saved_links.h"
#include "wifi_manager.h"
#include <esp_log.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace abarth::connection {
void switchMode(settings::ConnectionMode target) {
    if (target == settings::current().connection_mode) return;
    settings::setConnectionMode(target);
    if (target == settings::ConnectionMode::DirectCan) {
        // Ferma l'OBD senza cancellare dongle/rete salvati
        obd::obd().stopService();
        can::can().start();
    } else if (target == settings::ConnectionMode::BleObd) {
        can::can().stop();
        obd::obd().connectBLE();
    } else if (target == settings::ConnectionMode::WifiObd) {
        can::can().stop();
        obd::obd().connectWiFi();
    }
}
void autoConnectInternetWifi() {
    // in un task a parte, qualche secondo dopo l'avvio (UI e OBD gia' partiti)
    xTaskCreate([](void*) {
        vTaskDelay(pdMS_TO_TICKS(12000));
        links::WifiEntry e;
        auto& wm = wifi::WifiManager::instance();
        if (settings::current().connection_mode != settings::ConnectionMode::WifiObd && !wm.isConnected() &&
            links::lastWifi(links::WifiRole::Internet, e)) {
            ESP_LOGI("connection", "collegamento automatico alla rete salvata %s", e.ssid.c_str());
            wm.connect(e.ssid, e.pass, nullptr);   // se non c'e' o fallisce: nessun problema
        }
        vTaskDelete(nullptr);
    }, "wifi_auto", 4096, nullptr, 2, nullptr);
}
}  // namespace abarth::connection
