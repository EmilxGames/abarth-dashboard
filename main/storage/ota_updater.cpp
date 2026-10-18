#include "storage/ota_updater.h"

#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>
#include <esp_log.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace abarth::ota {

namespace {
constexpr const char* TAG = "ota_mgr";
constexpr size_t kChunkSize = 8192;
}

OtaManager& OtaManager::instance() {
    static OtaManager inst;
    return inst;
}

std::string OtaManager::getCurrentPartitionName() const {
    const esp_partition_t* running = esp_ota_get_running_partition();
    if (running && running->label[0] != '\0') {
        return running->label;
    }
    return "sconosciuta";
}

std::string OtaManager::getNextPartitionName() const {
    const esp_partition_t* next = esp_ota_get_next_update_partition(NULL);
    if (next && next->label[0] != '\0') {
        return next->label;
    }
    return "nessuna";
}

bool OtaManager::checkAndApplySdUpdate(ProgressCallback cb) {
    const char* candidates[] = {
        "/sdcard/update.bin",
        "/sdcard/firmware.bin"
    };

    const char* target_path = nullptr;
    for (const char* path : candidates) {
        struct stat st;
        if (stat(path, &st) == 0 && st.st_size > 100000) {
            target_path = path;
            break;
        }
    }

    if (!target_path) {
        return false;
    }

    FILE* f = fopen(target_path, "rb");
    if (!f) {
        ESP_LOGE(TAG, "Impossibile aprire %s per lettura", target_path);
        return false;
    }

    fseek(f, 0, SEEK_END);
    size_t file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    ESP_LOGI(TAG, "=== FILE AGGIORNAMENTO TROVATO SU MICROSD ===");
    ESP_LOGI(TAG, "File: %s (%zu bytes)", target_path, file_size);

    update_partition_ = esp_ota_get_next_update_partition(NULL);
    if (!update_partition_) {
        ESP_LOGE(TAG, "Nessuna partizione OTA disponibile");
        fclose(f);
        return false;
    }

    ESP_LOGI(TAG, "Partizione di destinazione OTA: %s (offset 0x%lx)",
             update_partition_->label, (unsigned long)update_partition_->address);

    if (cb) cb(0, "Inizializzazione flash OTA...");

    esp_err_t err = esp_ota_begin(update_partition_, file_size, &ota_handle_);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Errore esp_ota_begin: %s", esp_err_to_name(err));
        fclose(f);
        return false;
    }

    uint8_t* buffer = (uint8_t*)malloc(kChunkSize);
    if (!buffer) {
        ESP_LOGE(TAG, "Allocazione buffer fallita");
        esp_ota_abort(ota_handle_);
        fclose(f);
        return false;
    }

    size_t written = 0;
    int last_percent = -1;
    bool success = true;

    while (written < file_size) {
        size_t to_read = (file_size - written > kChunkSize) ? kChunkSize : (file_size - written);
        size_t n = fread(buffer, 1, to_read, f);
        if (n == 0) break;

        err = esp_ota_write(ota_handle_, buffer, n);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Errore scrittura chunk OTA: %s", esp_err_to_name(err));
            success = false;
            break;
        }

        written += n;
        int pct = static_cast<int>((written * 100) / file_size);
        if (pct != last_percent) {
            last_percent = pct;
            char msg[64];
            snprintf(msg, sizeof(msg), "Scrittura Firmware MicroSD: %d%%", pct);
            if (cb) cb(pct, msg);
            if (pct % 10 == 0) {
                ESP_LOGI(TAG, "Avanzamento OTA MicroSD: %d%% (%zu / %zu B)", pct, written, file_size);
            }
        }
    }

    free(buffer);
    fclose(f);

    if (!success || written != file_size) {
        ESP_LOGE(TAG, "Scrittura incompleta (%zu / %zu B)", written, file_size);
        esp_ota_abort(ota_handle_);
        return false;
    }

    if (cb) cb(99, "Finalizzazione e verifica firma...");

    err = esp_ota_end(ota_handle_);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Errore esp_ota_end: %s", esp_err_to_name(err));
        return false;
    }

    err = esp_ota_set_boot_partition(update_partition_);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Errore esp_ota_set_boot_partition: %s", esp_err_to_name(err));
        return false;
    }

    char done_path[64];
    snprintf(done_path, sizeof(done_path), "%s.done", target_path);
    rename(target_path, done_path);

    ESP_LOGI(TAG, "=== AGGIORNAMENTO COMPLETATO CON SUCCESSO! ===");
    ESP_LOGI(TAG, "File rinominato in: %s", done_path);
    ESP_LOGI(TAG, "Riavvio dispositivo in corso...");

    if (cb) cb(100, "Aggiornamento completato! Riavvio in corso...");
    vTaskDelay(pdMS_TO_TICKS(1500));
    esp_restart();
    return true;
}

esp_err_t OtaManager::beginWebOta(size_t total_size) {
    if (in_progress_) {
        esp_ota_abort(ota_handle_);
    }

    update_partition_ = esp_ota_get_next_update_partition(NULL);
    if (!update_partition_) {
        ESP_LOGE(TAG, "Nessuna partizione OTA disponibile per Web OTA");
        return ESP_ERR_NOT_FOUND;
    }

    esp_err_t err = esp_ota_begin(update_partition_, total_size, &ota_handle_);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Errore beginWebOta: %s", esp_err_to_name(err));
        return err;
    }

    in_progress_ = true;
    progress_percent_ = 0;
    written_bytes_ = 0;
    total_expected_bytes_ = total_size;
    ESP_LOGI(TAG, "Iniziata sessione Web OTA verso %s", update_partition_->label);
    return ESP_OK;
}

esp_err_t OtaManager::writeWebOtaChunk(const void* data, size_t len) {
    if (!in_progress_) return ESP_ERR_INVALID_STATE;

    esp_err_t err = esp_ota_write(ota_handle_, data, len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Errore writeWebOtaChunk: %s", esp_err_to_name(err));
        return err;
    }

    written_bytes_ += len;
    if (total_expected_bytes_ > 0) {
        progress_percent_ = static_cast<int>((written_bytes_ * 100) / total_expected_bytes_);
    }
    return ESP_OK;
}

esp_err_t OtaManager::endWebOta() {
    if (!in_progress_) return ESP_ERR_INVALID_STATE;

    esp_err_t err = esp_ota_end(ota_handle_);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Errore endWebOta: %s", esp_err_to_name(err));
        in_progress_ = false;
        return err;
    }

    err = esp_ota_set_boot_partition(update_partition_);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Errore set_boot_partition: %s", esp_err_to_name(err));
        in_progress_ = false;
        return err;
    }

    in_progress_ = false;
    progress_percent_ = 100;
    ESP_LOGI(TAG, "Web OTA completato con successo su %s!", update_partition_->label);
    return ESP_OK;
}

void OtaManager::reboot() {
    ESP_LOGI(TAG, "Riavvio su richiesta dopo OTA...");
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
}

}  // namespace abarth::ota
