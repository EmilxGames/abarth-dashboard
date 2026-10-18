#include "wifi_stream.h"
#include <esp_log.h>
#include <lwip/sockets.h>
#include <lwip/netdb.h>
#include <cstring>
#include "esp_timer.h"

namespace abarth::obd {

namespace {
constexpr const char* TAG = "wifi_stream";
}

WifiStream::WifiStream() {}

WifiStream::~WifiStream() {
    disconnect();
    if (rx_buf_) {
        vStreamBufferDelete(rx_buf_);
    }
}

bool WifiStream::begin(const WifiStreamConfig& cfg) {
    cfg_ = cfg;
    if (!rx_buf_) {
        rx_buf_ = xStreamBufferCreate(cfg_.rx_buffer_cap, 1);
        if (!rx_buf_) {
            ESP_LOGE(TAG, "Impossibile allocare rx buffer");
            return false;
        }
    }
    return true;
}

bool WifiStream::connect() {
    disconnect();
    
    struct sockaddr_in dest_addr;
    dest_addr.sin_addr.s_addr = inet_addr(cfg_.ip_address.c_str());
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(cfg_.port);

    sock_ = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (sock_ < 0) {
        ESP_LOGE(TAG, "Impossibile creare socket TCP: errno %d", errno);
        return false;
    }

    struct timeval tv;
    tv.tv_sec = 2; // 2 seconds connect timeout
    tv.tv_usec = 0;
    setsockopt(sock_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock_, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    ESP_LOGI(TAG, "Connessione TCP a %s:%d...", cfg_.ip_address.c_str(), cfg_.port);
    int err = ::connect(sock_, (struct sockaddr*)&dest_addr, sizeof(dest_addr));
    if (err != 0) {
        ESP_LOGE(TAG, "Socket unable to connect: errno %d", errno);
        close(sock_);
        sock_ = -1;
        return false;
    }

    ESP_LOGI(TAG, "Connesso con successo.");
    connected_ = true;
    
    xTaskCreate(WifiStream::rxTask, "wifi_rx", 4096, this, 5, &rx_task_);
    return true;
}

void WifiStream::disconnect() {
    connected_ = false;
    if (sock_ >= 0) {
        shutdown(sock_, 0);
        close(sock_);
        sock_ = -1;
    }
    if (rx_task_) {
        // Task check if connected_ == false and exit
        rx_task_ = nullptr;
    }
}

bool WifiStream::isConnected() const {
    return connected_.load() && sock_ >= 0;
}

size_t WifiStream::write(const uint8_t* data, size_t len) {
    if (!isConnected()) return 0;
    
    int written = send(sock_, data, len, 0);
    if (written < 0) {
        ESP_LOGE(TAG, "Errore send(): %d", errno);
        disconnect();
        return 0;
    }
    return written;
}

size_t WifiStream::write(const char* str) {
    return write(reinterpret_cast<const uint8_t*>(str), std::strlen(str));
}

size_t WifiStream::readUntil(char terminator, char* out, size_t out_max, uint32_t timeout_ms) {
    if (!isConnected() || !rx_buf_) return 0;

    size_t bytes_read = 0;
    const int64_t start_ms = esp_timer_get_time() / 1000;

    while (bytes_read < out_max - 1) {
        char c;
        size_t rx = xStreamBufferReceive(rx_buf_, &c, 1, pdMS_TO_TICKS(10));
        if (rx == 1) {
            if (c == terminator) {
                break;
            }
            out[bytes_read++] = c;
        }

        if (!isConnected()) {
            break;
        }

        const int64_t now_ms = esp_timer_get_time() / 1000;
        if ((now_ms - start_ms) >= timeout_ms) {
            ESP_LOGW(TAG, "readUntil timeout (%u ms) raggiunto, parziale=%u", 
                     static_cast<unsigned>(timeout_ms), static_cast<unsigned>(bytes_read));
            break;
        }
    }

    out[bytes_read] = '\0';
    return bytes_read;
}

void WifiStream::flushRx() {
    if (rx_buf_) {
        xStreamBufferReset(rx_buf_);
    }
}

void WifiStream::rxTask(void* arg) {
    auto* self = static_cast<WifiStream*>(arg);
    self->runRx();
    vTaskDelete(nullptr);
}

void WifiStream::runRx() {
    uint8_t buf[128];
    while (connected_) {
        int len = recv(sock_, buf, sizeof(buf), 0);
        if (len < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue; // Timeout timeout
            }
            ESP_LOGE(TAG, "recv failed: errno %d", errno);
            break;
        } else if (len == 0) {
            ESP_LOGI(TAG, "Connection closed by peer");
            break;
        } else {
            if (rx_buf_) {
                xStreamBufferSend(rx_buf_, buf, len, portMAX_DELAY);
            }
        }
    }
    
    connected_ = false;
    if (sock_ >= 0) {
        close(sock_);
        sock_ = -1;
    }
}

} // namespace abarth::obd
