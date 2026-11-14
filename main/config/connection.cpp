#include "config/connection.h"
#include <string>
#include <vector>
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
    // in un task a parte, qualche secondo dopo l'avvio (UI e OBD gia' partiti).
    // Prima si ascolta se la rete salvata c'e' (ricerca passiva: la radio non
    // trasmette); si prova a collegarsi solo se c'e'. In macchina, lontano da
    // casa, niente tentativi a vuoto ripetuti (ognuno faceva trasmettere la
    // radio su tutti i canali).
    xTaskCreate([](void*) {
        vTaskDelay(pdMS_TO_TICKS(12000));
        links::WifiEntry e;
        auto& wm = wifi::WifiManager::instance();
        if (settings::current().connection_mode == settings::ConnectionMode::WifiObd || wm.isConnected() ||
            !links::lastWifi(links::WifiRole::Internet, e)) {
            vTaskDelete(nullptr);
            return;
        }
        static volatile int found = -1;   // -1 in corso, 0 no, 1 si'
        static std::string want;
        want = e.ssid;
        found = -1;
        wm.startScan([](const std::vector<wifi::WifiNetwork>& nets) {
            int f = 0;
            for (const auto& n : nets) if (n.ssid == want) f = 1;
            found = f;
        }, true);
        for (int k = 0; k < 150 && found < 0; ++k) vTaskDelay(pdMS_TO_TICKS(100));
        if (found == 1) {
            for (int attempt = 0; attempt < 2 && !wm.isConnected(); ++attempt) {
                ESP_LOGI("connection", "rete salvata %s trovata: collegamento (tentativo %d)", e.ssid.c_str(), attempt + 1);
                wm.connect(e.ssid, e.pass, nullptr);
                for (int k = 0; k < 150 && !wm.isConnected(); ++k) vTaskDelay(pdMS_TO_TICKS(100));
            }
        } else {
            ESP_LOGI("connection", "rete salvata %s non nei paraggi: nessun collegamento automatico", e.ssid.c_str());
        }
        vTaskDelete(nullptr);
    }, "wifi_auto", 4096, nullptr, 2, nullptr);
}
}  // namespace abarth::connection
