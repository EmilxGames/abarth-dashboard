#include "obd/ble_stream.h"

#include <esp_log.h>

#include <algorithm>
#include <cinttypes>
#include <cstring>

namespace abarth::obd {

namespace {
constexpr const char* TAG = "ble_stream";
}  // namespace

#if ABARTH_HAS_BLE

class BleStream::ClientCallbacks : public NimBLEClientCallbacks {
public:
    explicit ClientCallbacks(BleStream* owner) : owner_(owner) {}

    void onConnect(NimBLEClient* /*client*/) override {
        ESP_LOGI(TAG, "client connesso");
    }

    void onDisconnect(NimBLEClient* /*client*/, int reason) override {
        ESP_LOGW(TAG, "client disconnesso (reason=%d)", reason);
        owner_->connected_.store(false);
    }

private:
    BleStream* owner_;
};

BleStream::BleStream() = default;

BleStream::~BleStream() {
    disconnect();
    if (rx_buf_) {
        vStreamBufferDelete(rx_buf_);
        rx_buf_ = nullptr;
    }
}

bool BleStream::begin(const BleStreamConfig& cfg) {
    cfg_ = cfg;
    if (!rx_buf_) {
        rx_buf_ = xStreamBufferCreate(cfg.rx_buffer_cap, 1);
        if (!rx_buf_) {
            ESP_LOGE(TAG, "impossibile allocare stream buffer RX");
            return false;
        }
    }
    
    static bool ble_initialized = false;
    if (!ble_initialized) {
        NimBLEDevice::init("abarth-dashboard");
        NimBLEDevice::setMTU(247);
        ble_initialized = true;
    }
    
    initialized_.store(true);
    return true;
}

bool BleStream::connect() {
    if (!initialized_.load()) return false;
    if (connected_.load()) return true;

    NimBLEScan* scan = NimBLEDevice::getScan();
    scan->setActiveScan(true);
    scan->setInterval(100);
    scan->setWindow(99);

    ESP_LOGI(TAG, "Scansione BLE (%" PRIu32 "s) in corso... (Target MAC: %s, Name: %s)",
             cfg_.scan_time_s,
             cfg_.target_mac.empty() ? "Nessuno (auto)" : cfg_.target_mac.c_str(),
             cfg_.device_name ? cfg_.device_name : "");

    NimBLEScanResults results = scan->getResults(cfg_.scan_time_s * 1000, false);

    const NimBLEAdvertisedDevice* target = nullptr;

    std::string want_mac = cfg_.target_mac;
    std::transform(want_mac.begin(), want_mac.end(), want_mac.begin(), ::tolower);

    for (int i = 0; i < results.getCount(); ++i) {
        const NimBLEAdvertisedDevice* dev = results.getDevice(i);
        std::string dev_mac = dev->getAddress().toString();
        std::transform(dev_mac.begin(), dev_mac.end(), dev_mac.begin(), ::tolower);

        if (!want_mac.empty()) {
            if (dev_mac == want_mac) {
                target = dev;
                ESP_LOGI(TAG, "Trovato dispositivo per MAC: %s (%s) RSSI=%d",
                         dev->getName().c_str(), dev->getAddress().toString().c_str(), dev->getRSSI());
                break;
            }
        } else {
            std::string dev_name = dev->getName();
            std::transform(dev_name.begin(), dev_name.end(), dev_name.begin(), ::tolower);

            bool match = false;
            if (cfg_.device_name && std::strlen(cfg_.device_name) > 0) {
                std::string want_name = cfg_.device_name;
                std::transform(want_name.begin(), want_name.end(), want_name.begin(), ::tolower);
                if (!dev_name.empty() && dev_name.find(want_name) != std::string::npos) {
                    match = true;
                }
            }

            if (!match && !dev_name.empty()) {
                if (dev_name.find("vlinker") != std::string::npos ||
                    dev_name.find("icar") != std::string::npos ||
                    dev_name.find("vgate") != std::string::npos ||
                    dev_name.find("obd") != std::string::npos ||
                    dev_name.find("v-link") != std::string::npos ||
                    dev_name.find("ios-vlink") != std::string::npos) {
                    match = true;
                }
            }

            if (!match && cfg_.service_uuid && std::strlen(cfg_.service_uuid) > 0) {
                if (dev->isAdvertisingService(NimBLEUUID(cfg_.service_uuid))) {
                    match = true;
                }
            }

            if (match) {
                target = dev;
                ESP_LOGI(TAG, "Trovato dongle OBD per nome/service: %s (%s) RSSI=%d",
                         dev->getName().c_str(), dev->getAddress().toString().c_str(), dev->getRSSI());
                break;
            }
        }
    }

    if (!target) {
        ESP_LOGW(TAG, "dongle non trovato nella scansione");
        scan->clearResults();
        return false;
    }

    if (!client_) {
        client_ = NimBLEDevice::createClient();
        static ClientCallbacks cb(this);
        client_->setClientCallbacks(&cb, false);
        // min interval=12 (15ms), max=24 (30ms), latency=0,
        // supervision timeout=400 (4000ms). Il timeout corto (510ms di prima)
        // faceva cadere il link quando il dongle era impegnato sul bus CAN.
        // 4s e' il valore raccomandato per OBD2-over-BLE (vGate, vLinker, ecc.).
        client_->setConnectionParams(12, 24, 0, 400);
    }

    if (!client_->connect(target)) {
        ESP_LOGE(TAG, "connect() a %s fallita", target->getAddress().toString().c_str());
        scan->clearResults();
        return false;
    }
    scan->clearResults();

    ESP_LOGI(TAG, "Connesso a %s. Avvio ricerca servizi/caratteristiche GATT...",
             target->getAddress().toString().c_str());

    notify_char_ = nullptr;
    write_char_  = nullptr;

    struct BleUuidPair {
        const char* service;
        const char* notify;
        const char* write;
    };

    static const BleUuidPair known_pairs[] = {
        { "FFF0", "FFF1", "FFF2" },
        { "0000FFF0-0000-1000-8000-00805F9B34FB", "0000FFF1-0000-1000-8000-00805F9B34FB", "0000FFF2-0000-1000-8000-00805F9B34FB" },
        { "FFE0", "FFE1", "FFE1" },
        { "FFE0", "FFE1", "FFE2" },
        { "0000FFE0-0000-1000-8000-00805F9B34FB", "0000FFE1-0000-1000-8000-00805F9B34FB", "0000FFE1-0000-1000-8000-00805F9B34FB" },
        { "E7810A71-73AE-499D-8C15-FAA9AEF0C3F2", "E7810A72-73AE-499D-8C15-FAA9AEF0C3F2", "E7810A73-73AE-499D-8C15-FAA9AEF0C3F2" },
        { "18F0", "2AF0", "2AF1" },
    };

    for (const auto& pair : known_pairs) {
        NimBLERemoteService* svc = client_->getService(NimBLEUUID(pair.service));
        if (svc) {
            NimBLERemoteCharacteristic* nc = svc->getCharacteristic(NimBLEUUID(pair.notify));
            NimBLERemoteCharacteristic* wc = svc->getCharacteristic(NimBLEUUID(pair.write));
            if (nc && wc) {
                notify_char_ = nc;
                write_char_  = wc;
                ESP_LOGI(TAG, "Trovato servizio OBD noto: %s (Notify=%s, Write=%s)",
                         pair.service, pair.notify, pair.write);
                break;
            }
        }
    }

    if (!notify_char_ || !write_char_) {
        ESP_LOGI(TAG, "Servizi noti non trovati, avvio ispezione dinamica GATT...");
        const auto& services = client_->getServices(true);
        for (auto* s : services) {
            if (!s) continue;
            if (s->getUUID() == NimBLEUUID((uint16_t)0x1800) ||
                s->getUUID() == NimBLEUUID((uint16_t)0x1801) ||
                s->getUUID() == NimBLEUUID((uint16_t)0x180A)) {
                continue;
            }
            const auto& chars = s->getCharacteristics(true);
            NimBLERemoteCharacteristic* fn = nullptr;
            NimBLERemoteCharacteristic* fw = nullptr;
            for (auto* c : chars) {
                if (!c) continue;
                if ((c->canNotify() || c->canIndicate()) && !fn) fn = c;
                if ((c->canWrite() || c->canWriteNoResponse()) && !fw) fw = c;
            }
            if (fn && fw) {
                notify_char_ = fn;
                write_char_  = fw;
                ESP_LOGI(TAG, "Servizio OBD dinamico identificato: %s (Notify=%s, Write=%s)",
                         s->getUUID().toString().c_str(),
                         notify_char_->getUUID().toString().c_str(),
                         write_char_->getUUID().toString().c_str());
                break;
            }
        }
    }

    if (!notify_char_ || !write_char_) {
        ESP_LOGE(TAG, "Impossibile trovare caratteristiche di Notify e Write sul dispositivo BLE");
        client_->disconnect();
        return false;
    }

    if (notify_char_->canNotify() || notify_char_->canIndicate()) {
        if (!notify_char_->subscribe(
                true,
                [this](NimBLERemoteCharacteristic* c, uint8_t* d, size_t l, bool n) {
                    onNotify(c, d, l, n);
                })) {
            ESP_LOGE(TAG, "Sottoscrizione alle notifiche fallita");
            client_->disconnect();
            return false;
        }
    } else {
        ESP_LOGE(TAG, "La caratteristica non supporta NOTIFY / INDICATE");
        client_->disconnect();
        return false;
    }

    connected_.store(true);
    ESP_LOGI(TAG, "BLE connesso e sottoscritto con successo!");
    return true;
}

std::vector<std::string> BleStream::scanDevices(uint32_t scan_time_s) {
    std::vector<std::string> ret;
    if (!initialized_.load()) return ret;

    NimBLEScan* scan = NimBLEDevice::getScan();
    scan->setActiveScan(true);
    scan->setInterval(100);
    scan->setWindow(99);

    ESP_LOGI(TAG, "scansione manuale BLE (%" PRIu32 "s)...", scan_time_s);
    NimBLEScanResults results = scan->getResults(scan_time_s * 1000, false);

    for (int i = 0; i < results.getCount(); ++i) {
        const NimBLEAdvertisedDevice* dev = results.getDevice(i);
        std::string name = dev->getName();
        if (name.empty()) name = "Dispositivo Sconosciuto";
        ret.push_back(name + " [" + dev->getAddress().toString() + "]");
    }
    scan->clearResults();
    return ret;
}

void BleStream::disconnect() {
    if (client_) {
        if (client_->isConnected()) client_->disconnect();
        NimBLEDevice::deleteClient(client_);
        client_ = nullptr;
    }
    notify_char_ = nullptr;
    write_char_  = nullptr;
    connected_.store(false);
}

size_t BleStream::write(const uint8_t* data, size_t len) {
    if (!connected_.load() || !write_char_) return 0;
    const size_t max_frag = NimBLEDevice::getMTU() > 3 ? NimBLEDevice::getMTU() - 3 : 20;
    size_t sent = 0;
    const bool response = write_char_->canWrite();
    while (sent < len) {
        const size_t frag = std::min(max_frag, len - sent);
        if (!write_char_->writeValue(data + sent, frag, response)) break;
        sent += frag;
    }
    return sent;
}

size_t BleStream::write(const char* str) {
    return write(reinterpret_cast<const uint8_t*>(str), std::strlen(str));
}

void BleStream::onNotify(NimBLERemoteCharacteristic* /*chr*/, uint8_t* data, size_t len,
                         bool /*isNotify*/) {
    if (!rx_buf_) return;
    xStreamBufferSend(rx_buf_, data, len, 0);
}

size_t BleStream::readUntil(char terminator, char* out, size_t out_max, uint32_t timeout_ms) {
    if (!rx_buf_ || out_max == 0) return 0;
    const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);
    size_t pos = 0;
    while (pos + 1 < out_max) {
        TickType_t now = xTaskGetTickCount();
        if (now >= deadline) break;
        char c = 0;
        const TickType_t wait = deadline - now;
        size_t n = xStreamBufferReceive(rx_buf_, &c, 1, wait);
        if (n == 0) break;
        if (c == terminator) break;
        out[pos++] = c;
    }
    out[pos] = '\0';
    return pos;
}

void BleStream::flushRx() {
    if (rx_buf_) xStreamBufferReset(rx_buf_);
}

#else  // !ABARTH_HAS_BLE  --- implementazione stub ------------------------------

BleStream::BleStream()  = default;
BleStream::~BleStream() = default;

bool BleStream::begin(const BleStreamConfig& cfg) {
    cfg_ = cfg;
    ESP_LOGW(TAG, "BLE disabilitato in build: il dongle OBD non verra' contattato.");
    ESP_LOGW(TAG, "Su ESP32-P4 serve un host BLE via esp_hosted + ESP32-C6 (non ancora in arduino-esp32 3.1.x).");
    initialized_.store(true);
    return true;
}

bool BleStream::connect() {
    return false;
}

void BleStream::disconnect() {
    connected_.store(false);
}

size_t BleStream::write(const uint8_t*, size_t) { return 0; }
size_t BleStream::write(const char*)            { return 0; }

size_t BleStream::readUntil(char /*terminator*/, char* out, size_t out_max,
                            uint32_t timeout_ms) {
    if (out_max == 0) return 0;
    vTaskDelay(pdMS_TO_TICKS(timeout_ms));
    out[0] = '\0';
    return 0;
}

void BleStream::flushRx() {}

#endif  // ABARTH_HAS_BLE

}  // namespace abarth::obd
