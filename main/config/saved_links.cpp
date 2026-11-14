#include "config/saved_links.h"

#include <algorithm>
#include <cstring>
#include <strings.h>
#include <mutex>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <nvs.h>

#include "config/settings.h"

namespace abarth::links {
namespace {
constexpr const char* TAG = "links";
constexpr const char* kNs = "abarth_lk";
constexpr const char* kKeyWifi = "wifi_v1";
constexpr const char* kKeyBle = "ble_v1";
constexpr uint8_t kVersion = 1;

// Formato in NVS (dimensioni fisse)
struct WifiRec { char ssid[33]; char pass[65]; uint8_t role; uint8_t used; uint32_t seq; };
struct BleRec { char name[32]; char mac[18]; uint8_t used; uint8_t pad; uint32_t seq; };
struct WifiBlob { uint8_t version; uint8_t pad[3]; WifiRec r[kMaxWifi]; };
struct BleBlob { uint8_t version; uint8_t pad[3]; BleRec r[kMaxBle]; };

std::mutex g_mx;
// In PSRAM: la RAM interna all'avvio e' al limite
WifiBlob* g_wifi = nullptr;
BleBlob* g_ble = nullptr;
uint32_t g_seq = 0;

void copy_str(char* dst, size_t n, const std::string& s) {
    std::strncpy(dst, s.c_str(), n - 1);
    dst[n - 1] = '\0';
}

bool valid_str(const char* s, size_t n) { return std::memchr(s, '\0', n) != nullptr; }

void save_locked() {
    nvs_handle_t h;
    if (nvs_open(kNs, NVS_READWRITE, &h) != ESP_OK) return;
    esp_err_t e1 = nvs_set_blob(h, kKeyWifi, g_wifi, sizeof(WifiBlob));
    esp_err_t e2 = nvs_set_blob(h, kKeyBle, g_ble, sizeof(BleBlob));
    if (e1 == ESP_OK && e2 == ESP_OK) nvs_commit(h);
    else ESP_LOGE(TAG, "salvataggio non riuscito: %s / %s", esp_err_to_name(e1), esp_err_to_name(e2));
    nvs_close(h);
}

int find_wifi_locked(const std::string& ssid) {
    for (int i = 0; i < kMaxWifi; ++i)
        if (g_wifi->r[i].used && ssid == g_wifi->r[i].ssid) return i;
    return -1;
}

int find_ble_locked(const std::string& mac) {
    for (int i = 0; i < kMaxBle; ++i)
        if (g_ble->r[i].used && strcasecmp(mac.c_str(), g_ble->r[i].mac) == 0) return i;
    return -1;
}

/// posto libero oppure quello usato meno di recente
template <typename R>
int slot_for(R* recs, int n) {
    int best = 0;
    for (int i = 0; i < n; ++i) {
        if (!recs[i].used) return i;
        if (recs[i].seq < recs[best].seq) best = i;
    }
    return best;
}

void add_wifi_locked(const std::string& ssid, const std::string& pass, WifiRole role) {
    if (ssid.empty() || ssid.size() > 32 || pass.size() > 64) return;
    int i = find_wifi_locked(ssid);
    if (i < 0) i = slot_for(g_wifi->r, kMaxWifi);
    WifiRec& r = g_wifi->r[i];
    copy_str(r.ssid, sizeof(r.ssid), ssid);
    copy_str(r.pass, sizeof(r.pass), pass);
    r.role = static_cast<uint8_t>(role);
    r.used = 1;
    r.seq = ++g_seq;
}

void add_ble_locked(const std::string& name, const std::string& mac) {
    if (mac.empty() || mac.size() > 17) return;
    int i = find_ble_locked(mac);
    const bool known = i >= 0;
    if (!known) i = slot_for(g_ble->r, kMaxBle);
    BleRec& r = g_ble->r[i];
    if (!known) std::memset(&r, 0, sizeof(r));
    copy_str(r.mac, sizeof(r.mac), mac);
    if (!name.empty()) copy_str(r.name, sizeof(r.name), name);
    r.used = 1;
    r.seq = ++g_seq;
}

/// Carica (una volta sola) con validazione e importazione dal sistema precedente
void ensure_loaded_locked() {
    if (g_wifi) return;
    g_wifi = static_cast<WifiBlob*>(heap_caps_calloc(1, sizeof(WifiBlob), MALLOC_CAP_SPIRAM));
    g_ble = static_cast<BleBlob*>(heap_caps_calloc(1, sizeof(BleBlob), MALLOC_CAP_SPIRAM));
    if (!g_wifi || !g_ble) {   // senza PSRAM: memoria normale
        if (!g_wifi) g_wifi = static_cast<WifiBlob*>(calloc(1, sizeof(WifiBlob)));
        if (!g_ble) g_ble = static_cast<BleBlob*>(calloc(1, sizeof(BleBlob)));
    }
    bool have_w = false, have_b = false;
    nvs_handle_t h;
    if (nvs_open(kNs, NVS_READONLY, &h) == ESP_OK) {
        size_t sz = sizeof(WifiBlob);
        have_w = nvs_get_blob(h, kKeyWifi, g_wifi, &sz) == ESP_OK && sz == sizeof(WifiBlob) && g_wifi->version == kVersion;
        sz = sizeof(BleBlob);
        have_b = nvs_get_blob(h, kKeyBle, g_ble, &sz) == ESP_OK && sz == sizeof(BleBlob) && g_ble->version == kVersion;
        nvs_close(h);
    }
    if (!have_w) std::memset(g_wifi, 0, sizeof(WifiBlob));
    if (!have_b) std::memset(g_ble, 0, sizeof(BleBlob));
    g_wifi->version = kVersion;
    g_ble->version = kVersion;
    // validazione: stringhe terminate, uso noto
    for (auto& r : g_wifi->r) {
        if (r.used && (!valid_str(r.ssid, sizeof(r.ssid)) || !valid_str(r.pass, sizeof(r.pass)) || r.ssid[0] == '\0' || r.role > 1))
            std::memset(&r, 0, sizeof(r));
        g_seq = std::max(g_seq, r.seq);
    }
    for (auto& r : g_ble->r) {
        if (r.used && (!valid_str(r.name, sizeof(r.name)) || !valid_str(r.mac, sizeof(r.mac)) || r.mac[0] == '\0'))
            std::memset(&r, 0, sizeof(r));
        g_seq = std::max(g_seq, r.seq);
    }
    // prima volta: importa rete e dongle salvati col sistema precedente
    if (!have_w || !have_b) {
        const auto s = settings::current();
        if (!have_w && !s.saved_wifi_ssid.empty()) add_wifi_locked(s.saved_wifi_ssid, s.saved_wifi_pass, WifiRole::Dongle);
        if (!have_b && !s.saved_dongle_mac.empty()) add_ble_locked("Dongle OBD", s.saved_dongle_mac);
        save_locked();
        ESP_LOGI(TAG, "elenchi creati (importati: rete %d, dongle %d)", !s.saved_wifi_ssid.empty(), !s.saved_dongle_mac.empty());
    }
}
}  // namespace

std::vector<WifiEntry> wifiList() {
    std::lock_guard<std::mutex> l(g_mx);
    ensure_loaded_locked();
    std::vector<WifiEntry> v;
    for (const auto& r : g_wifi->r)
        if (r.used) v.push_back({r.ssid, r.pass, static_cast<WifiRole>(r.role), r.seq});
    std::sort(v.begin(), v.end(), [](const WifiEntry& a, const WifiEntry& b) { return a.seq > b.seq; });
    return v;
}

std::vector<BleEntry> bleList() {
    std::lock_guard<std::mutex> l(g_mx);
    ensure_loaded_locked();
    std::vector<BleEntry> v;
    for (const auto& r : g_ble->r)
        if (r.used) v.push_back({r.name, r.mac, r.seq});
    std::sort(v.begin(), v.end(), [](const BleEntry& a, const BleEntry& b) { return a.seq > b.seq; });
    return v;
}

void addWifi(const std::string& ssid, const std::string& pass, WifiRole role) {
    std::lock_guard<std::mutex> l(g_mx);
    ensure_loaded_locked();
    add_wifi_locked(ssid, pass, role);
    save_locked();
}

void setWifiRole(const std::string& ssid, WifiRole role) {
    std::lock_guard<std::mutex> l(g_mx);
    ensure_loaded_locked();
    const int i = find_wifi_locked(ssid);
    if (i < 0) return;
    g_wifi->r[i].role = static_cast<uint8_t>(role);
    save_locked();
}

void removeWifi(const std::string& ssid) {
    std::lock_guard<std::mutex> l(g_mx);
    ensure_loaded_locked();
    const int i = find_wifi_locked(ssid);
    if (i < 0) return;
    std::memset(&g_wifi->r[i], 0, sizeof(WifiRec));
    save_locked();
}

bool findWifi(const std::string& ssid, WifiEntry& out) {
    std::lock_guard<std::mutex> l(g_mx);
    ensure_loaded_locked();
    const int i = find_wifi_locked(ssid);
    if (i < 0) return false;
    const auto& r = g_wifi->r[i];
    out = {r.ssid, r.pass, static_cast<WifiRole>(r.role), r.seq};
    return true;
}

bool lastWifi(WifiRole role, WifiEntry& out) {
    for (const auto& e : wifiList())
        if (e.role == role) { out = e; return true; }
    return false;
}

void addBle(const std::string& name, const std::string& mac) {
    std::lock_guard<std::mutex> l(g_mx);
    ensure_loaded_locked();
    add_ble_locked(name, mac);
    save_locked();
}

void removeBle(const std::string& mac) {
    std::lock_guard<std::mutex> l(g_mx);
    ensure_loaded_locked();
    const int i = find_ble_locked(mac);
    if (i < 0) return;
    std::memset(&g_ble->r[i], 0, sizeof(BleRec));
    save_locked();
}

const char* roleName(WifiRole r) { return r == WifiRole::Dongle ? "Dongle OBD" : "Internet / web server"; }

}  // namespace abarth::links
