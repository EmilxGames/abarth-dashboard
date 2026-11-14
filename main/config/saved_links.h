#pragma once
/**
 * @file saved_links.h
 * @brief Collegamenti salvati, come su un telefono: fino a 8 reti Wi-Fi
 *        (SSID, password, uso: internet/web server oppure dongle OBD) e fino a
 *        4 dongle Bluetooth (nome, MAC). Aggiungere non toglie mai nulla:
 *        quando la lista e' piena esce l'elemento usato meno di recente.
 *        Cambiare modalita' OBD non cancella niente. Salvato in NVS
 *        (versionato e validato); al primo avvio si importano la rete e il
 *        dongle salvati con il sistema precedente. Thread-safe.
 */
#include <cstdint>
#include <string>
#include <vector>

namespace abarth::links {

constexpr int kMaxWifi = 8;
constexpr int kMaxBle = 4;

enum class WifiRole : uint8_t { Internet = 0, Dongle = 1 };

struct WifiEntry {
    std::string ssid, pass;
    WifiRole role = WifiRole::Internet;
    uint32_t seq = 0;   ///< piu' alto = usato piu' di recente
};
struct BleEntry {
    std::string name, mac;
    uint32_t seq = 0;
};

/// Copie degli elenchi, dal piu' recente al meno recente
std::vector<WifiEntry> wifiList();
std::vector<BleEntry> bleList();

/// Aggiunge o aggiorna (password/uso) e la segna come usata ora
void addWifi(const std::string& ssid, const std::string& pass, WifiRole role);
void setWifiRole(const std::string& ssid, WifiRole role);
void removeWifi(const std::string& ssid);
bool findWifi(const std::string& ssid, WifiEntry& out);
/// Rete con quell'uso usata per ultima (false se nessuna)
bool lastWifi(WifiRole role, WifiEntry& out);

/// Aggiunge o aggiorna (il nome vuoto non cancella quello gia' noto)
void addBle(const std::string& name, const std::string& mac);
void removeBle(const std::string& mac);

const char* roleName(WifiRole r);

}  // namespace abarth::links
