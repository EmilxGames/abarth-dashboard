#pragma once

#include "obd/istream.h"
#include <string>
#include <atomic>
#include "freertos/FreeRTOS.h"
#include "freertos/stream_buffer.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

namespace abarth::obd {

struct WifiStreamConfig {
    std::string ip_address = "192.168.0.10";
    uint16_t    port       = 35000;
    size_t      rx_buffer_cap = 4096;
};

class WifiStream : public IStream {
public:
    WifiStream();
    ~WifiStream() override;

    bool begin(const WifiStreamConfig& cfg);

    bool connect() override;
    void disconnect() override;
    bool isConnected() const override;

    size_t write(const uint8_t* data, size_t len) override;
    size_t write(const char* str) override;

    size_t readUntil(char terminator, char* out, size_t out_max, uint32_t timeout_ms) override;
    void flushRx() override;

private:
    static void rxTask(void* arg);
    void runRx(int fd);

    // Tutti i metodi (tranne il task RX) sono chiamati solo dal task OBD.
    WifiStreamConfig     cfg_{};
    int                  sock_            = -1;
    StreamBufferHandle_t rx_buf_          = nullptr;
    std::atomic<bool>    connected_{false};
    TaskHandle_t         rx_task_         = nullptr;
    SemaphoreHandle_t    rx_done_         = nullptr;  ///< dato dal task RX quando esce
};

} // namespace abarth::obd
