#pragma once

#include <string>
#include <vector>
#include <functional>

#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"

namespace abarth::wifi {

struct WifiNetwork {
    std::string ssid;
    int rssi;
    wifi_auth_mode_t auth_mode;
};

using ScanCallback = std::function<void(const std::vector<WifiNetwork>&)>;
using ConnectCallback = std::function<void(bool success, const std::string& ip_address, const std::string& gateway_ip)>;

class WifiManager {
public:
    static WifiManager& instance();

    /// Inizializza il modulo WiFi (chiama esp_netif_init, esp_event_loop_create_default)
    void init();

    /// Avvia una scansione delle reti in background. Il callback viene chiamato asincronamente.
    void startScan(ScanCallback cb);

    /// Avvia la connessione a un AP. Il callback viene chiamato asincronamente.
    void connect(const std::string& ssid, const std::string& password, ConnectCallback cb);

    /// Disconnette e ferma il WiFi
    void disconnect();

    /// Ritorna true se siamo connessi a un AP e abbiamo un IP
    bool isConnected() const;
    std::string getCurrentIP() const { return current_ip_; }
    std::string getCurrentSSID() const { return current_ssid_; }
    std::string getCurrentGateway() const { return current_gw_; }
    std::string getCurrentNetmask() const { return current_netmask_; }
    std::string getCurrentDNS() const { return current_dns_; }

private:
    WifiManager() = default;
    ~WifiManager() = default;

    static void eventHandler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);

    esp_netif_t* netif_ = nullptr;
    bool initialized_ = false;
    bool scanning_ = false;
    bool connected_ = false;
    std::string current_ip_;
    std::string current_ssid_;
    std::string current_gw_;
    std::string current_netmask_;
    std::string current_dns_;
    
    ScanCallback scan_cb_;
    ConnectCallback connect_cb_;
};

} // namespace abarth::wifi
