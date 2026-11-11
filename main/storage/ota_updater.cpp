#include "storage/ota_updater.h"

#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>
#include <esp_log.h>
#include <esp_system.h>
#include <esp_app_desc.h>
#include <esp_app_format.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace abarth::ota {

namespace {
constexpr const char* TAG = "ota_mgr";
constexpr size_t kChunkSize = 8192;
// esp_app_desc_t segue l'header immagine e il primo header di segmento
constexpr size_t kAppDescOffset = sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t);
constexpr size_t kAppDescEnd = kAppDescOffset + sizeof(esp_app_desc_t);

/// Verifica che il blocco iniziale di un .bin sia un'app ESP valida per QUESTO progetto.
/// Ritorna ESP_OK se valida, altrimenti un errore (e scrive il motivo nel log).
esp_err_t validate_app_header(const uint8_t* data, size_t len, esp_app_desc_t* out_desc) {
    if (len < kAppDescEnd) {
        ESP_LOGE(TAG, "Immagine troppo corta per l'header (%zu B)", len);
        return ESP_ERR_INVALID_SIZE;
    }
    const auto* hdr = reinterpret_cast<const esp_image_header_t*>(data);
    if (hdr->magic != ESP_IMAGE_HEADER_MAGIC) {
        ESP_LOGE(TAG, "Magic immagine non valido (0x%02x)", hdr->magic);
        return ESP_ERR_INVALID_VERSION;
    }
    if (hdr->chip_id != ESP_CHIP_ID_ESP32P4) {
        ESP_LOGE(TAG, "Immagine per un altro chip (chip_id=%d)", (int)hdr->chip_id);
        return ESP_ERR_INVALID_VERSION;
    }
    esp_app_desc_t desc;
    std::memcpy(&desc, data + kAppDescOffset, sizeof(desc));
    if (desc.magic_word != ESP_APP_DESC_MAGIC_WORD) {
        ESP_LOGE(TAG, "esp_app_desc non trovato (magic 0x%08lx)", (unsigned long)desc.magic_word);
        return ESP_ERR_INVALID_VERSION;
    }
    const esp_app_desc_t* running = esp_app_get_description();
    if (std::strncmp(desc.project_name, running->project_name, sizeof(desc.project_name)) != 0) {
        ESP_LOGE(TAG, "Progetto diverso: '%.32s' (atteso '%.32s')", desc.project_name, running->project_name);
        return ESP_ERR_INVALID_VERSION;
    }
    ESP_LOGI(TAG, "Firmware valido: %.32s ver %.32s (%.16s %.16s)",
             desc.project_name, desc.version, desc.date, desc.time);
    if (out_desc) *out_desc = desc;
    return ESP_OK;
}

/// Toglie di mezzo il file di aggiornamento in modo che NON venga riapplicato al prossimo avvio.
/// Prova a rinominarlo in <dest>, se fallisce lo cancella. Ritorna false se il file e' ancora li'.
bool retire_update_file(const char* path, const char* suffix) {
    char dest[96];
    snprintf(dest, sizeof(dest), "%s%s", path, suffix);
    unlink(dest);  // FatFs non sovrascrive con rename: rimuovi prima il vecchio
    if (rename(path, dest) == 0) {
        ESP_LOGI(TAG, "File rinominato in: %s", dest);
        return true;
    }
    ESP_LOGW(TAG, "Rename %s -> %s fallito, cancello il file", path, dest);
    if (unlink(path) == 0) return true;
    struct stat st;
    return stat(path, &st) != 0;
}
}  // namespace

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

    // Validazione header prima di toccare la flash
    {
        uint8_t head[kAppDescEnd];
        size_t n = fread(head, 1, sizeof(head), f);
        fseek(f, 0, SEEK_SET);
        esp_app_desc_t new_desc;
        if (validate_app_header(head, n, &new_desc) != ESP_OK) {
            ESP_LOGE(TAG, "File non valido per questo dispositivo: lo scarto");
            fclose(f);
            retire_update_file(target_path, ".invalid");
            return false;
        }
        const esp_app_desc_t* running = esp_app_get_description();
        if (std::memcmp(new_desc.app_elf_sha256, running->app_elf_sha256, sizeof(new_desc.app_elf_sha256)) == 0) {
            ESP_LOGW(TAG, "Il firmware su SD e' identico a quello in esecuzione: nessun aggiornamento");
            fclose(f);
            retire_update_file(target_path, ".done");
            return false;
        }
    }

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

    // Prima di cambiare la partizione di boot togliamo update.bin dalla SD:
    // se restasse li', al riavvio verrebbe riflashato all'infinito (bootloop).
    if (!retire_update_file(target_path, ".done")) {
        ESP_LOGE(TAG, "Impossibile rinominare o cancellare %s: aggiornamento annullato per evitare un bootloop", target_path);
        return false;
    }

    err = esp_ota_set_boot_partition(update_partition_);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Errore esp_ota_set_boot_partition: %s", esp_err_to_name(err));
        return false;
    }

    ESP_LOGI(TAG, "=== AGGIORNAMENTO COMPLETATO CON SUCCESSO! ===");
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

    if (written_bytes_ == 0) {
        esp_err_t verr = validate_app_header(static_cast<const uint8_t*>(data), len, nullptr);
        if (verr != ESP_OK) {
            esp_ota_abort(ota_handle_);
            in_progress_ = false;
            return verr;
        }
    }

    esp_err_t err = esp_ota_write(ota_handle_, data, len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Errore writeWebOtaChunk: %s", esp_err_to_name(err));
        esp_ota_abort(ota_handle_);
        in_progress_ = false;
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
