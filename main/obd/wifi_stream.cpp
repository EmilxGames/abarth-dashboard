#include "wifi_stream.h"
#include <esp_log.h>
#include <lwip/sockets.h>
#include <lwip/netdb.h>
#include <fcntl.h>
#include <cstring>
#include "esp_timer.h"

namespace abarth::obd {

namespace {
constexpr const char* TAG = "wifi_stream";
constexpr int kConnectTimeoutMs = 3000;
constexpr int kRecvPollMs       = 200;   // il task RX controlla connected_ a questo ritmo
constexpr int kSendTimeoutMs    = 2000;

struct RxArgs { WifiStream* self; int fd; };
}

WifiStream::WifiStream() {}

WifiStream::~WifiStream() {
    disconnect();
    if (rx_buf_) {
        vStreamBufferDelete(rx_buf_);
    }
    if (rx_done_) {
        vSemaphoreDelete(rx_done_);
    }
}

bool WifiStream::begin(const WifiStreamConfig& cfg) {
    cfg_ = cfg;
    if (!rx_done_) {
        rx_done_ = xSemaphoreCreateBinary();
    }
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
    if (!rx_buf_ || !rx_done_) return false;
    xStreamBufferReset(rx_buf_);

    struct sockaddr_in dest_addr = {};
    dest_addr.sin_addr.s_addr = inet_addr(cfg_.ip_address.c_str());
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(cfg_.port);

    int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (fd < 0) {
        ESP_LOGE(TAG, "Impossibile creare socket TCP: errno %d", errno);
        return false;
    }

    // Connect non bloccante con timeout: lwIP puo' ignorare SO_SNDTIMEO sul connect
    ESP_LOGI(TAG, "Connessione TCP a %s:%d...", cfg_.ip_address.c_str(), cfg_.port);
    const int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    int err = ::connect(fd, (struct sockaddr*)&dest_addr, sizeof(dest_addr));
    if (err != 0 && errno != EINPROGRESS) {
        ESP_LOGE(TAG, "connect fallito: errno %d", errno);
        close(fd);
        return false;
    }
    if (err != 0) {
        fd_set wset;
        FD_ZERO(&wset);
        FD_SET(fd, &wset);
        struct timeval tv = { kConnectTimeoutMs / 1000, (kConnectTimeoutMs % 1000) * 1000 };
        const int sel = select(fd + 1, nullptr, &wset, nullptr, &tv);
        int so_err = 0;
        socklen_t so_len = sizeof(so_err);
        if (sel <= 0 || getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_err, &so_len) != 0 || so_err != 0) {
            ESP_LOGE(TAG, "connect timeout/errore (sel=%d, so_err=%d)", sel, so_err);
            close(fd);
            return false;
        }
    }
    fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);

    struct timeval rtv = { 0, kRecvPollMs * 1000 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &rtv, sizeof(rtv));
    struct timeval stv = { kSendTimeoutMs / 1000, (kSendTimeoutMs % 1000) * 1000 };
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &stv, sizeof(stv));

    ESP_LOGI(TAG, "Connesso con successo.");
    sock_ = fd;
    connected_ = true;

    xSemaphoreTake(rx_done_, 0);  // azzera un eventuale segnale vecchio
    auto* args = new RxArgs{this, fd};
    if (xTaskCreate(WifiStream::rxTask, "wifi_rx", 4096, args, 5, &rx_task_) != pdPASS) {
        delete args;
        rx_task_ = nullptr;
        connected_ = false;
        close(fd);
        sock_ = -1;
        return false;
    }
    return true;
}

void WifiStream::disconnect() {
    connected_ = false;
    if (sock_ >= 0) {
        // Sblocca recv() nel task RX, poi aspetta che esca prima di chiudere:
        // cosi' il task non lavora mai su un descrittore gia' chiuso o riusato.
        shutdown(sock_, SHUT_RDWR);
    }
    if (rx_task_) {
        if (xSemaphoreTake(rx_done_, pdMS_TO_TICKS(1500)) != pdTRUE) {
            ESP_LOGE(TAG, "task RX non terminato: socket lasciato aperto per sicurezza");
            rx_task_ = nullptr;
            sock_ = -1;
            return;
        }
        rx_task_ = nullptr;
    }
    if (sock_ >= 0) {
        close(sock_);
        sock_ = -1;
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
    auto* a = static_cast<RxArgs*>(arg);
    WifiStream* self = a->self;
    const int fd = a->fd;
    delete a;
    self->runRx(fd);
    xSemaphoreGive(self->rx_done_);
    vTaskDelete(nullptr);
}

void WifiStream::runRx(int fd) {
    // Lavora solo sulla copia locale del descrittore e non lo chiude mai:
    // la chiusura spetta a disconnect(), dopo che questo task e' uscito.
    uint8_t buf[128];
    while (connected_) {
        int len = recv(fd, buf, sizeof(buf), 0);
        if (len < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;  // timeout di polling: ricontrolla connected_
            }
            if (connected_) ESP_LOGE(TAG, "recv failed: errno %d", errno);
            break;
        } else if (len == 0) {
            ESP_LOGI(TAG, "Connection closed by peer");
            break;
        } else if (rx_buf_) {
            // Invio limitato: se nessuno legge, si scartano i dati invece di bloccarsi
            if (xStreamBufferSend(rx_buf_, buf, len, pdMS_TO_TICKS(100)) < static_cast<size_t>(len)) {
                ESP_LOGW(TAG, "rx buffer pieno: dati scartati");
            }
        }
    }
    connected_ = false;
}

} // namespace abarth::obd
