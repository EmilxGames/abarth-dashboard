#include "wifi_manager.h"

#include <esp_log.h>
#include <esp_wifi.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <esp_netif.h>
#include <nvs_flash.h>
#include <cstring>
#include <algorithm>
#include "storage/rtc_manager.h"
#include "storage/web_server.h"

namespace abarth::wifi {

namespace {
constexpr const char* TAG = "wifi_mgr";
}

WifiManager& WifiManager::instance() {
    static WifiManager inst;
    return inst;
}

bool WifiManager::init() {
    if (initialized_) return true;

    // L'inizializzazione del netif/event loop è di solito fatta in app_main o qui
    esp_netif_init();
    esp_event_loop_create_default();   // ESP_ERR_INVALID_STATE se esiste gia': va bene
    if (!netif_) netif_ = esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t err = esp_wifi_init(&cfg);
    if (err != ESP_OK) {
        last_error_ = std::string("avvio Wi-Fi non riuscito (") + esp_err_to_name(err) + ")";
        ESP_LOGE(TAG, "esp_wifi_init: %s", esp_err_to_name(err));
        return false;
    }

    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &WifiManager::eventHandler, this, nullptr);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &WifiManager::eventHandler, this, nullptr);

    esp_wifi_set_mode(WIFI_MODE_STA);
    // Italia: canali 1-13 (con il paese "mondo" i canali 12-13 restano esclusi)
    esp_wifi_set_country_code("IT", true);
    err = esp_wifi_start();
    if (err != ESP_OK) {
        last_error_ = std::string("avvio Wi-Fi non riuscito (") + esp_err_to_name(err) + ")";
        ESP_LOGE(TAG, "esp_wifi_start: %s", esp_err_to_name(err));
        return false;
    }

    initialized_ = true;
    last_error_.clear();
    ESP_LOGI(TAG, "WiFi Manager inizializzato");
    return true;
}

void WifiManager::startScan(ScanCallback cb, bool passive) {
    if (scanning_) {
        ESP_LOGW(TAG, "Scansione gia' in corso");
        if (cb) cb({});
        return;
    }
    scan_cb_ = std::move(cb);
    scanning_ = true;
    scan_passive_ = passive;
    // Avvio radio e ricerca in un task a parte: subito dopo esp_wifi_start il
    // co-processore Wi-Fi (C6, via esp_hosted) non e' ancora pronto e la
    // ricerca fallisce con ESP_ERR_WIFI_STATE (era il motivo per cui la
    // ricerca non trovava mai niente). Si riprova per qualche secondo, senza
    // bloccare l'interfaccia.
    xTaskCreate([](void* arg) {
        auto* self = static_cast<WifiManager*>(arg);
        esp_err_t err = ESP_FAIL;
        if (self->initialized_ || self->init()) {
            wifi_scan_config_t scan_config = {};
            scan_config.show_hidden = true;
            if (self->scan_passive_) {
                scan_config.scan_type = WIFI_SCAN_TYPE_PASSIVE;
                scan_config.scan_time.passive = 300;   // ms per canale
            }
            // Niente disconnessione: la ricerca funziona anche con una rete
            // collegata, cosi' cercare non fa perdere il collegamento attuale
            for (int attempt = 0; attempt < 20; ++attempt) {
                err = esp_wifi_scan_start(&scan_config, false);
                if (err != ESP_ERR_WIFI_STATE) break;
                vTaskDelay(pdMS_TO_TICKS(250));
            }
            if (err != ESP_OK) self->last_error_ = std::string("ricerca non riuscita (") + esp_err_to_name(err) + ")";
            else self->last_error_.clear();
        }
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Errore avvio scansione: %s", esp_err_to_name(err));
            self->scanning_ = false;
            if (self->scan_cb_) {
                auto cb2 = std::move(self->scan_cb_);
                self->scan_cb_ = nullptr;
                cb2({});
            }
        }
        vTaskDelete(nullptr);
    }, "wifi_scan", 4096, this, 3, nullptr);
}

void WifiManager::connect(const std::string& ssid, const std::string& password, ConnectCallback cb) {
    connect_cb_ = std::move(cb);
    connected_ = false;
    current_ip_ = "";
    current_ssid_ = ssid;
    pending_ssid_ = ssid;
    pending_pass_ = password;
    // come la ricerca: avvio radio e tentativi in un task, senza bloccare l'interfaccia
    xTaskCreate([](void* arg) {
        auto* self = static_cast<WifiManager*>(arg);
        esp_err_t err = ESP_FAIL;
        if (self->initialized_ || self->init()) {
            wifi_config_t wifi_config = {};
            std::strncpy(reinterpret_cast<char*>(wifi_config.sta.ssid), self->pending_ssid_.c_str(), sizeof(wifi_config.sta.ssid) - 1);
            std::strncpy(reinterpret_cast<char*>(wifi_config.sta.password), self->pending_pass_.c_str(), sizeof(wifi_config.sta.password) - 1);
            // reti aperte (molti dongle) oppure WPA2 o superiore
            wifi_config.sta.threshold.authmode = self->pending_pass_.empty() ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA_PSK;
            // cerca su tutti i canali e prende il punto di accesso con il segnale migliore
            wifi_config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
            wifi_config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
            wifi_config.sta.pmf_cfg.capable = true;
            wifi_config.sta.pmf_cfg.required = false;
            esp_wifi_disconnect();
            esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
            ESP_LOGI(TAG, "Tentativo connessione a SSID: %s", self->pending_ssid_.c_str());
            for (int attempt = 0; attempt < 20; ++attempt) {
                err = esp_wifi_connect();
                if (err != ESP_ERR_WIFI_STATE) break;
                vTaskDelay(pdMS_TO_TICKS(250));
            }
        }
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Errore avvio connessione: %s", esp_err_to_name(err));
            if (self->last_error_.empty()) self->last_error_ = std::string("connessione non avviata (") + esp_err_to_name(err) + ")";
            if (self->connect_cb_) {
                auto cb2 = std::move(self->connect_cb_);
                self->connect_cb_ = nullptr;
                cb2(false, "", "");
            }
        }
        vTaskDelete(nullptr);
    }, "wifi_conn", 4096, this, 3, nullptr);
}

void WifiManager::disconnect() {
    if (initialized_) {
        esp_wifi_disconnect();
        connected_ = false;
        current_ip_ = "";
        current_ssid_ = "";
        current_gw_ = "";
        current_netmask_ = "";
        current_dns_ = "";
    }
}

bool WifiManager::isConnected() const {
    return connected_;
}

void WifiManager::eventHandler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    auto* self = static_cast<WifiManager*>(arg);

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_SCAN_DONE) {
        ESP_LOGI(TAG, "Scansione WiFi completata");
        self->scanning_ = false;

        uint16_t ap_count = 0;
        esp_wifi_scan_get_ap_num(&ap_count);
        
        std::vector<WifiNetwork> networks;
        if (ap_count > 0) {
            wifi_ap_record_t* ap_info = new wifi_ap_record_t[ap_count];
            if (esp_wifi_scan_get_ap_records(&ap_count, ap_info) == ESP_OK) {
                for (int i = 0; i < ap_count; i++) {
                    WifiNetwork net;
                    net.ssid = reinterpret_cast<char*>(ap_info[i].ssid);
                    net.rssi = ap_info[i].rssi;
                    net.auth_mode = ap_info[i].authmode;
                    ESP_LOGI(TAG, "rete %s: canale %u, sicurezza %d, %d dBm", net.ssid.c_str(), ap_info[i].primary,
                             (int)ap_info[i].authmode, ap_info[i].rssi);
                    
                    // Deduplicate
                    auto it = std::find_if(networks.begin(), networks.end(), [&](const WifiNetwork& n) {
                        return n.ssid == net.ssid;
                    });
                    if (it == networks.end() && !net.ssid.empty()) {
                        networks.push_back(net);
                    }
                }
            }
            delete[] ap_info;
        }
        
        // Sort by RSSI
        std::sort(networks.begin(), networks.end(), [](const WifiNetwork& a, const WifiNetwork& b) {
            return a.rssi > b.rssi;
        });

        if (self->scan_cb_) {
            self->scan_cb_(networks);
            self->scan_cb_ = nullptr;
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        const auto* ev = static_cast<wifi_event_sta_disconnected_t*>(event_data);
        const uint8_t reason = ev ? ev->reason : 0;
        ESP_LOGI(TAG, "WiFi disconnesso (motivo %u)", reason);
        self->connected_ = false;
        abarth::web::stopServer();
        // L'uscita volontaria dalla rete precedente (cambio rete) non e' un
        // fallimento della nuova connessione: la richiamata aspetta l'esito vero
        if (reason == WIFI_REASON_ASSOC_LEAVE && self->connect_cb_) return;
        self->current_ssid_ = "";
        if (self->connect_cb_) {
            self->connect_cb_(false, "", "");
            self->connect_cb_ = nullptr;
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = static_cast<ip_event_got_ip_t*>(event_data);
        char ip_str[32];
        esp_ip4addr_ntoa(&event->ip_info.ip, ip_str, sizeof(ip_str));
        
        char gw_str[32];
        esp_ip4addr_ntoa(&event->ip_info.gw, gw_str, sizeof(gw_str));

        char nm_str[32];
        esp_ip4addr_ntoa(&event->ip_info.netmask, nm_str, sizeof(nm_str));

        ESP_LOGI(TAG, "WiFi connesso! IP: %s, GW: %s", ip_str, gw_str);
        
        self->connected_ = true;
        self->current_ip_ = ip_str;
        self->current_gw_ = gw_str;
        self->current_netmask_ = nm_str;

        esp_netif_dns_info_t dns_info;
        if (esp_netif_get_dns_info(self->netif_, ESP_NETIF_DNS_MAIN, &dns_info) == ESP_OK) {
            char dns_str[32];
            esp_ip4addr_ntoa((const esp_ip4_addr_t*)&dns_info.ip.u_addr.ip4, dns_str, sizeof(dns_str));
            self->current_dns_ = dns_str;
        } else {
            self->current_dns_ = "N/A";
        }

        if (self->connect_cb_) {
            self->connect_cb_(true, self->current_ip_, gw_str);
            self->connect_cb_ = nullptr;
        }

        // Avvia sincronizzazione oraria NTP via internet per aggiornare RTC hardware
        abarth::storage::rtc().enableNtpSync();

        // Il Web Server NON parte piu' in automatico: si avvia solo dal pulsante
        // in Impostazioni (e si spegne da solo dopo 10 minuti).
    }
}

} // namespace abarth::wifi
