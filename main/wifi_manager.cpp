#include "wifi_manager.h"

#include <esp_log.h>
#include <esp_wifi.h>
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

void WifiManager::init() {
    if (initialized_) return;

    // L'inizializzazione del netif/event loop è di solito fatta in app_main o qui
    esp_netif_init();
    esp_event_loop_create_default();
    netif_ = esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &WifiManager::eventHandler, this, nullptr);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &WifiManager::eventHandler, this, nullptr);

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_start();

    initialized_ = true;
    ESP_LOGI(TAG, "WiFi Manager inizializzato");
}

void WifiManager::startScan(ScanCallback cb) {
    if (!initialized_) init();
    if (scanning_) {
        ESP_LOGW(TAG, "Scansione gia' in corso");
        if (cb) cb({});
        return;
    }

    scan_cb_ = std::move(cb);
    scanning_ = true;

    wifi_scan_config_t scan_config = {};
    scan_config.show_hidden = true;
    
    // Disconnect if we are trying to scan and connect might be blocking it (optional, but good for reliable scanning)
    esp_wifi_disconnect();

    esp_err_t err = esp_wifi_scan_start(&scan_config, false);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Errore avvio scansione: %s", esp_err_to_name(err));
        scanning_ = false;
        if (scan_cb_) {
            scan_cb_({});
            scan_cb_ = nullptr;
        }
    }
}

void WifiManager::connect(const std::string& ssid, const std::string& password, ConnectCallback cb) {
    if (!initialized_) init();
    
    connect_cb_ = std::move(cb);
    connected_ = false;
    current_ip_ = "";
    current_ssid_ = ssid;

    wifi_config_t wifi_config = {};
    std::strncpy(reinterpret_cast<char*>(wifi_config.sta.ssid), ssid.c_str(), sizeof(wifi_config.sta.ssid) - 1);
    std::strncpy(reinterpret_cast<char*>(wifi_config.sta.password), password.c_str(), sizeof(wifi_config.sta.password) - 1);
    
    // For OBD dongles, they might be open or WPA2
    wifi_config.sta.threshold.authmode = password.empty() ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;

    esp_wifi_disconnect();
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    
    ESP_LOGI(TAG, "Tentativo connessione a SSID: %s", ssid.c_str());
    esp_err_t err = esp_wifi_connect();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Errore avvio connessione: %s", esp_err_to_name(err));
        if (connect_cb_) {
            connect_cb_(false, "", "");
            connect_cb_ = nullptr;
        }
    }
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
        ESP_LOGI(TAG, "WiFi disconnesso");
        self->connected_ = false;
        self->current_ssid_ = "";
        abarth::web::stopServer();
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

        // Avvia automaticamente il Web Server HTTP su porta 80
        abarth::web::startServer();
    }
}

} // namespace abarth::wifi
