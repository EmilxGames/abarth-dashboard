#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <functional>
#include "esp_err.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"

namespace abarth::ota {

using ProgressCallback = std::function<void(int percent, const char* status)>;

class OtaManager {
public:
    static OtaManager& instance();

    /// Controlla se sulla MicroSD è presente un firmware da installare
    /// (/sdcard/update.bin o /sdcard/firmware.bin). Se presente, esegue il flash
    /// e riavvia automaticamente il dispositivo.
    bool checkAndApplySdUpdate(ProgressCallback cb = nullptr);

    /// Inizia una sessione OTA per upload da Web Server
    esp_err_t beginWebOta(size_t total_size = OTA_SIZE_UNKNOWN);

    /// Scrive un chunk di dati binari ricevuto via Web
    esp_err_t writeWebOtaChunk(const void* data, size_t len);

    /// Conclude la sessione OTA, verifica la firma e imposta la nuova partizione di boot
    esp_err_t endWebOta();

    /// Riavvia l'ESP32 dopo un aggiornamento riuscito
    void reboot();

    bool isOtaInProgress() const { return in_progress_; }
    int getProgressPercent() const { return progress_percent_; }
    std::string getCurrentPartitionName() const;
    std::string getNextPartitionName() const;

private:
    OtaManager() = default;
    ~OtaManager() = default;

    bool in_progress_ = false;
    int progress_percent_ = 0;
    size_t written_bytes_ = 0;
    size_t total_expected_bytes_ = 0;
    esp_ota_handle_t ota_handle_ = 0;
    const esp_partition_t* update_partition_ = nullptr;
};

inline OtaManager& ota() { return OtaManager::instance(); }

}  // namespace abarth::ota
