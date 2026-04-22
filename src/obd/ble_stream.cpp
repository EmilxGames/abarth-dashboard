#include "obd/ble_stream.h"

#include <esp_log.h>

#include <algorithm>
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
    if (!initialized_.load()) {
        NimBLEDevice::init("abarth-dashboard");
        NimBLEDevice::setMTU(247);
        initialized_.store(true);
    }
    return true;
}

bool BleStream::connect() {
    if (!initialized_.load()) return false;
    if (connected_.load()) return true;

    NimBLEScan* scan = NimBLEDevice::getScan();
    scan->setActiveScan(true);
    scan->setInterval(100);
    scan->setWindow(99);

    ESP_LOGI(TAG, "scansione BLE (%us) in corso...", cfg_.scan_time_s);
    NimBLEScanResults results = scan->getResults(cfg_.scan_time_s * 1000, false);

    const NimBLEAdvertisedDevice* target = nullptr;
    NimBLEUUID want_svc(cfg_.service_uuid);
    for (int i = 0; i < results.getCount(); ++i) {
        const NimBLEAdvertisedDevice* dev = results.getDevice(i);
        const bool match_name = !dev->getName().empty() &&
                                dev->getName().find(cfg_.device_name) != std::string::npos;
        const bool match_svc = dev->isAdvertisingService(want_svc);
        if (match_name || match_svc) {
            target = dev;
            ESP_LOGI(TAG, "trovato: %s (%s) RSSI=%d",
                     dev->getName().c_str(), dev->getAddress().toString().c_str(), dev->getRSSI());
            break;
        }
    }
    scan->clearResults();
    if (!target) {
        ESP_LOGW(TAG, "dongle non trovato");
        return false;
    }

    client_ = NimBLEDevice::createClient();
    static ClientCallbacks cb(this);
    client_->setClientCallbacks(&cb, false);
    client_->setConnectionParams(12, 12, 0, 51);

    if (!client_->connect(target)) {
        ESP_LOGE(TAG, "connect() fallita");
        NimBLEDevice::deleteClient(client_);
        client_ = nullptr;
        return false;
    }

    NimBLERemoteService* svc = client_->getService(NimBLEUUID(cfg_.service_uuid));
    if (!svc) {
        ESP_LOGE(TAG, "service %s non presente", cfg_.service_uuid);
        client_->disconnect();
        return false;
    }
    notify_char_ = svc->getCharacteristic(NimBLEUUID(cfg_.notify_uuid));
    write_char_  = svc->getCharacteristic(NimBLEUUID(cfg_.write_uuid));
    if (!notify_char_ || !write_char_) {
        ESP_LOGE(TAG, "caratteristiche %s/%s mancanti",
                 cfg_.notify_uuid, cfg_.write_uuid);
        client_->disconnect();
        return false;
    }

    if (notify_char_->canNotify()) {
        notify_char_->subscribe(
            true,
            [this](NimBLERemoteCharacteristic* c, uint8_t* d, size_t l, bool n) {
                onNotify(c, d, l, n);
            });
    } else {
        ESP_LOGE(TAG, "caratteristica %s non supporta NOTIFY", cfg_.notify_uuid);
        client_->disconnect();
        return false;
    }

    connected_.store(true);
    ESP_LOGI(TAG, "collegato e sottoscritto alle notifiche");
    return true;
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
    while (sent < len) {
        const size_t frag = std::min(max_frag, len - sent);
        if (!write_char_->writeValue(data + sent, frag, false)) break;
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
