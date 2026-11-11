#include "sd_card_manager.h"

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/unistd.h>
#include <dirent.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "driver/sdmmc_host.h"
#include "driver/sdmmc_default_configs.h"
#include "sdmmc_cmd.h"
#include "lvgl.h"
#include "ui/oil_blue_png.h"
#include "ui/abarth_logo_png.h"
#include "ui/abarth_splash_png.h"

static const char* TAG = "SD_CARD";

#define MOUNT_POINT "/sdcard"

// Pin mapping verificato per Guition JC1060P470C (ESP32-P4)
#define SD_PIN_CLK  GPIO_NUM_43
#define SD_PIN_CMD  GPIO_NUM_44
#define SD_PIN_D0   GPIO_NUM_39
#define SD_PIN_D1   GPIO_NUM_40
#define SD_PIN_D2   GPIO_NUM_41
#define SD_PIN_D3   GPIO_NUM_42

static sdmmc_card_t* s_card = nullptr;
static lv_fs_drv_t   s_lv_fs_drv;

// LVGL FS Driver callbacks
static void* sd_lv_fs_open(lv_fs_drv_t* drv, const char* path, lv_fs_mode_t mode) {
    LV_UNUSED(drv);
    char full_path[256];

    // Accetta sia "S:/icons/..." che "S:icons/..."
    if (path[0] == '/') {
        snprintf(full_path, sizeof(full_path), "%s%s", MOUNT_POINT, path);
    } else {
        snprintf(full_path, sizeof(full_path), "%s/%s", MOUNT_POINT, path);
    }

    const char* fmode = "rb";
    if (mode == LV_FS_MODE_WR) {
        fmode = "wb";
    } else if (mode == (LV_FS_MODE_WR | LV_FS_MODE_RD)) {
        fmode = "w+b";
    }

    FILE* f = fopen(full_path, fmode);
    if (!f) {
        ESP_LOGW(TAG, "LVGL fopen FALLITO per: '%s' (mode='%s', errno=%d: %s)", full_path, fmode, errno, strerror(errno));
        return nullptr;
    }
    return (void*)f;
}

static lv_fs_res_t sd_lv_fs_close(lv_fs_drv_t* drv, void* file_p) {
    LV_UNUSED(drv);
    if (!file_p) return LV_FS_RES_INV_PARAM;
    fclose((FILE*)file_p);
    return LV_FS_RES_OK;
}

static lv_fs_res_t sd_lv_fs_read(lv_fs_drv_t* drv, void* file_p, void* buf, uint32_t btr, uint32_t* br) {
    LV_UNUSED(drv);
    if (!file_p || !buf) return LV_FS_RES_INV_PARAM;
    size_t read_bytes = fread(buf, 1, btr, (FILE*)file_p);
    if (br) *br = (uint32_t)read_bytes;
    return LV_FS_RES_OK;
}

static lv_fs_res_t sd_lv_fs_write(lv_fs_drv_t* drv, void* file_p, const void* buf, uint32_t btw, uint32_t* bw) {
    LV_UNUSED(drv);
    if (!file_p || !buf) return LV_FS_RES_INV_PARAM;
    size_t written_bytes = fwrite(buf, 1, btw, (FILE*)file_p);
    if (bw) *bw = (uint32_t)written_bytes;
    return LV_FS_RES_OK;
}

static lv_fs_res_t sd_lv_fs_seek(lv_fs_drv_t* drv, void* file_p, uint32_t pos, lv_fs_whence_t whence) {
    LV_UNUSED(drv);
    if (!file_p) return LV_FS_RES_INV_PARAM;
    int w = SEEK_SET;
    if (whence == LV_FS_SEEK_CUR) w = SEEK_CUR;
    else if (whence == LV_FS_SEEK_END) w = SEEK_END;
    if (fseek((FILE*)file_p, pos, w) != 0) {
        ESP_LOGW(TAG, "LVGL fseek FALLITO: pos=%u whence=%d (errno=%d)", (unsigned)pos, (int)whence, (int)errno);
        return LV_FS_RES_UNKNOWN;
    }
    return LV_FS_RES_OK;
}

static lv_fs_res_t sd_lv_fs_tell(lv_fs_drv_t* drv, void* file_p, uint32_t* pos_p) {
    LV_UNUSED(drv);
    if (!file_p || !pos_p) return LV_FS_RES_INV_PARAM;
    long pos = ftell((FILE*)file_p);
    if (pos < 0) {
        ESP_LOGW(TAG, "LVGL ftell FALLITO (errno=%d)", errno);
        return LV_FS_RES_UNKNOWN;
    }
    *pos_p = (uint32_t)pos;
    return LV_FS_RES_OK;
}

#include "sd_pwr_ctrl_by_on_chip_ldo.h"
#include "esp_ldo_regulator.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace abarth::storage {

SDCardManager& SDCardManager::instance() {
    static SDCardManager s_instance;
    return s_instance;
}

bool SDCardManager::init() {
    ESP_LOGI(TAG, "Inizializzazione MicroSD su slot SDMMC nativo (Guition JC1060P470C)...");

    // GPIO 45: Controllo alimentazione slot TF (gate di Q1 AO3401 se R10 montato)
    gpio_config_t pwr_pin_cfg = {
        .pin_bit_mask = (1ULL << 45),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&pwr_pin_cfg);
    gpio_set_level(GPIO_NUM_45, 0); // LOW = P-Channel ON (conduce TF_VCC)
    vTaskDelay(pdMS_TO_TICKS(50));

    // Sull'ESP32-P4 i pin SDMMC Slot 0 (GPIO 39-48) sono alimentati dall'LDO interno Channel 4.
    sd_pwr_ctrl_ldo_config_t ldo_config = {
        .ldo_chan_id = 4,
    };
    sd_pwr_ctrl_handle_t pwr_ctrl_handle = nullptr;
    esp_err_t pwr_ret = sd_pwr_ctrl_new_on_chip_ldo(&ldo_config, &pwr_ctrl_handle);
    if (pwr_ret == ESP_OK && pwr_ctrl_handle) {
        esp_err_t vret = sd_pwr_ctrl_set_io_voltage(pwr_ctrl_handle, 3300);
        ESP_LOGI(TAG, "LDO Channel 4 configurato a 3300 mV (ret=%s)", esp_err_to_name(vret));
        vTaskDelay(pdMS_TO_TICKS(100));
    } else {
        ESP_LOGW(TAG, "LDO Channel 4 non disponibile (%s)", esp_err_to_name(pwr_ret));
    }

    esp_ldo_dump(stdout);

    // Abilita pullup interni solo sulle linee dati e comando (NON su CLK!)
    gpio_pullup_en(SD_PIN_D0);
    gpio_pullup_en(SD_PIN_D1);
    gpio_pullup_en(SD_PIN_D2);
    gpio_pullup_en(SD_PIN_D3);
    gpio_pullup_en(SD_PIN_CMD);
    gpio_pullup_dis(SD_PIN_CLK);

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 8,
        .allocation_unit_size = 16 * 1024,
        .disk_status_check_enable = false
    };

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.slot = SDMMC_HOST_SLOT_0;
    host.max_freq_khz = SDMMC_FREQ_DEFAULT; // 20 MHz
    host.pwr_ctrl_handle = pwr_ctrl_handle;
    host.init = []() -> esp_err_t { return ESP_OK; };
    host.deinit = []() -> esp_err_t { return ESP_OK; };

    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.width = 4;
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    ESP_LOGI(TAG, "Tentativo montaggio SDMMC Slot 0 a 4-bit...");
    esp_err_t ret = esp_vfs_fat_sdmmc_mount(MOUNT_POINT, &host, &slot_config, &mount_config, &s_card);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Tentativo 4-bit fallito (%s), ritento con GPIO45=1...", esp_err_to_name(ret));
        gpio_set_level(GPIO_NUM_45, 1);
        vTaskDelay(pdMS_TO_TICKS(100));
        ret = esp_vfs_fat_sdmmc_mount(MOUNT_POINT, &host, &slot_config, &mount_config, &s_card);
    }
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Tentativo fallito (%s), ritento in modalita' 1-bit con GPIO45=0...", esp_err_to_name(ret));
        gpio_set_level(GPIO_NUM_45, 0);
        slot_config.width = 1;
        vTaskDelay(pdMS_TO_TICKS(100));
        ret = esp_vfs_fat_sdmmc_mount(MOUNT_POINT, &host, &slot_config, &mount_config, &s_card);
    }

    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "MicroSD non rilevata o montaggio fallito (codice %s). Il cruscotto continuera' con la memoria interna.",
                 esp_err_to_name(ret));
        m_mounted = false;
        return false;
    }

    m_mounted = true;
    m_total_size_mb = ((uint64_t)s_card->csd.capacity) * s_card->csd.sector_size / (1024 * 1024);
    ESP_LOGI(TAG, "MicroSD montata con successo su %s! Capacita': %llu MB", MOUNT_POINT, m_total_size_mb);
    sdmmc_card_print_info(stdout, s_card);

    // Registra il driver filesystem di LVGL per abilitare "S:/..."
    registerLvglFsDriver();

    // Elenca i file presenti nella cartella icons per diagnostica
    DIR* dir = opendir(MOUNT_POINT "/icons");
    if (dir) {
        struct dirent* ent;
        int count = 0;
        ESP_LOGI(TAG, "Scansione file in %s/icons:", MOUNT_POINT);
        while ((ent = readdir(dir)) != NULL) {
            ESP_LOGI(TAG, "  [%d] %s", ++count, ent->d_name);
        }
        closedir(dir);
        ESP_LOGI(TAG, "Totale file icone trovati: %d", count);
    } else {
        ESP_LOGE(TAG, "Impossibile aprire directory %s/icons (errno=%d)", MOUNT_POINT, errno);
    }

    // Assicurati che le cartelle standard esistano
    mkdir(MOUNT_POINT "/logs", 0775);
    mkdir(MOUNT_POINT "/config", 0775);
    mkdir(MOUNT_POINT "/icons", 0775);
    mkdir(MOUNT_POINT "/backgrounds", 0775);

    // Inizializza il file di telemetria per la sessione corrente
    time_t now = time(nullptr);
    struct tm ti;
    localtime_r(&now, &ti);
    if (ti.tm_year + 1900 >= 2024) {
        snprintf(m_active_log_path, sizeof(m_active_log_path),
                 "%s/logs/telemetria_%04d%02d%02d_%02d%02d%02d.csv",
                 MOUNT_POINT,
                 ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday,
                 ti.tm_hour, ti.tm_min, ti.tm_sec);
    } else {
        snprintf(m_active_log_path, sizeof(m_active_log_path), "%s/logs/telemetria.csv", MOUNT_POINT);
    }
    ESP_LOGI(TAG, "File di log telemetria attivo: %s", m_active_log_path);
    FILE* f = fopen(m_active_log_path, "r");
    if (!f) {
        // Se non esiste, crea intestazione CSV
        f = fopen(m_active_log_path, "w");
        if (f) {
            fputs("timestamp_ms,rpm,speed_kmh,boost_bar,coolant_temp_c,oil_temp_virtual_c,iat_c,throttle_pct,age_max_ms\n", f);
            fclose(f);
        }
    } else {
        fclose(f);
    }

    return true;
}

uint64_t SDCardManager::getFreeSizeMB() const {
    if (!m_mounted) return 0;
    FATFS *fs;
    DWORD fre_clust, fre_sect;
    if (f_getfree("0:", &fre_clust, &fs) == FR_OK) {
        fre_sect = fre_clust * fs->csize;
        return ((uint64_t)fre_sect * 512) / (1024 * 1024);
    }
    return 0;
}

void SDCardManager::registerLvglFsDriver() {
    lv_fs_drv_init(&s_lv_fs_drv);
    s_lv_fs_drv.letter = 'S';
    s_lv_fs_drv.open_cb  = sd_lv_fs_open;
    s_lv_fs_drv.close_cb = sd_lv_fs_close;
    s_lv_fs_drv.read_cb  = sd_lv_fs_read;
    s_lv_fs_drv.write_cb = sd_lv_fs_write;
    s_lv_fs_drv.seek_cb  = sd_lv_fs_seek;
    s_lv_fs_drv.tell_cb  = sd_lv_fs_tell;

    lv_fs_drv_register(&s_lv_fs_drv);
    ESP_LOGI(TAG, "Driver filesystem LVGL registrato come unita' 'S:' (S:/icons/...)");

#if LV_USE_LODEPNG
    ESP_LOGI(TAG, "Decoder LodePNG per LVGL attivo");
#elif LV_USE_PNG
    lv_png_init();
    ESP_LOGI(TAG, "Decoder PNG per LVGL inizializzato con successo");
#endif

    // Assicura che l'icona termometro blu per motore freddo esista su MicroSD
    struct stat st_oil;
    if (stat("/sdcard/icons/oil_blue.png", &st_oil) != 0 || (size_t)st_oil.st_size != g_oil_blue_png_len) {
        FILE* f = fopen("/sdcard/icons/oil_blue.png", "wb");
        if (f) {
            fwrite(g_oil_blue_png, 1, g_oil_blue_png_len, f);
            fclose(f);
            ESP_LOGI(TAG, "Icona S:/icons/oil_blue.png scritta con successo su SD (%u bytes)", (unsigned)g_oil_blue_png_len);
        } else {
            ESP_LOGW(TAG, "Impossibile scrivere S:/icons/oil_blue.png (%s)", strerror(errno));
        }
    }

    // Assicura che il vero scudetto Abarth esista su MicroSD
    struct stat st_logo;
    if (stat("/sdcard/icons/abarth_logo.png", &st_logo) != 0 || (size_t)st_logo.st_size != g_abarth_logo_png_len) {
        FILE* f = fopen("/sdcard/icons/abarth_logo.png", "wb");
        if (f) {
            fwrite(g_abarth_logo_png, 1, g_abarth_logo_png_len, f);
            fclose(f);
            ESP_LOGI(TAG, "Icona S:/icons/abarth_logo.png scritta con successo su SD (%u bytes)", (unsigned)g_abarth_logo_png_len);
        } else {
            ESP_LOGW(TAG, "Impossibile scrivere S:/icons/abarth_logo.png (%s)", strerror(errno));
        }
    }

    // Sostituisce anche lo scorpion.png (vecchia emoji) con il vero scudetto Abarth
    struct stat st_scorp;
    if (stat("/sdcard/icons/scorpion.png", &st_scorp) != 0 || (size_t)st_scorp.st_size != g_abarth_logo_png_len) {
        FILE* f = fopen("/sdcard/icons/scorpion.png", "wb");
        if (f) {
            fwrite(g_abarth_logo_png, 1, g_abarth_logo_png_len, f);
            fclose(f);
            ESP_LOGI(TAG, "Aggiornato S:/icons/scorpion.png con il vero scudetto Abarth");
        }
    }

    // Assicura che l'emblema scudetto grande Abarth per lo splash esista su MicroSD
    struct stat st_splash;
    if (stat("/sdcard/icons/abarth_splash.png", &st_splash) != 0 || (size_t)st_splash.st_size != g_abarth_splash_png_len) {
        FILE* f = fopen("/sdcard/icons/abarth_splash.png", "wb");
        if (f) {
            fwrite(g_abarth_splash_png, 1, g_abarth_splash_png_len, f);
            fclose(f);
            ESP_LOGI(TAG, "Icona S:/icons/abarth_splash.png scritta con successo su SD (%u bytes)", (unsigned)g_abarth_splash_png_len);
        } else {
            ESP_LOGW(TAG, "Impossibile scrivere S:/icons/abarth_splash.png (%s)", strerror(errno));
        }
    }

}

bool SDCardManager::logTelemetry(const char* csv_line) {
    if (!m_mounted || !csv_line || m_active_log_path[0] == '\0') return false;

    FILE* f = fopen(m_active_log_path, "a");
    if (!f) return false;

    fputs(csv_line, f);
    const size_t len = strlen(csv_line);
    if (len == 0 || csv_line[len - 1] != '\n') {
        fputc('\n', f);
    }
    fclose(f);
    return true;
}

size_t SDCardManager::getLogFileCount() const {
    if (!m_mounted) return 0;
    DIR* dir = opendir(MOUNT_POINT "/logs");
    if (!dir) return 0;
    size_t count = 0;
    struct dirent* ent;
    while ((ent = readdir(dir)) != nullptr) {
        if (ent->d_name[0] != '.') {
            count++;
        }
    }
    closedir(dir);
    return count;
}

uint64_t SDCardManager::getLogFilesTotalBytes() const {
    if (!m_mounted) return 0;
    DIR* dir = opendir(MOUNT_POINT "/logs");
    if (!dir) return 0;
    uint64_t total = 0;
    struct dirent* ent;
    char path[384];
    while ((ent = readdir(dir)) != nullptr) {
        if (ent->d_name[0] != '.') {
            snprintf(path, sizeof(path), "%s/logs/%s", MOUNT_POINT, ent->d_name);
            struct stat st;
            if (stat(path, &st) == 0) {
                total += st.st_size;
            }
        }
    }
    closedir(dir);
    return total;
}

bool SDCardManager::startNewLogSession() {
    if (!m_mounted) return false;
    time_t now = time(nullptr);
    struct tm ti;
    localtime_r(&now, &ti);
    if (ti.tm_year + 1900 >= 2024) {
        snprintf(m_active_log_path, sizeof(m_active_log_path),
                 "%s/logs/telemetria_%04d%02d%02d_%02d%02d%02d.csv",
                 MOUNT_POINT,
                 ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday,
                 ti.tm_hour, ti.tm_min, ti.tm_sec);
    } else {
        snprintf(m_active_log_path, sizeof(m_active_log_path), "%s/logs/telemetria_%lu.csv", MOUNT_POINT, (unsigned long)now);
    }

    FILE* f = fopen(m_active_log_path, "w");
    if (f) {
        fputs("timestamp_ms,rpm,speed_kmh,boost_bar,coolant_temp_c,oil_temp_virtual_c,iat_c,throttle_pct,age_max_ms\n", f);
        fclose(f);
        ESP_LOGI(TAG, "Nuova sessione di log telemetria avviata: %s", m_active_log_path);
        return true;
    }
    return false;
}

bool SDCardManager::clearLogs() {
    if (!m_mounted) return false;
    DIR* dir = opendir(MOUNT_POINT "/logs");
    if (!dir) return false;
    struct dirent* ent;
    char path[384];
    while ((ent = readdir(dir)) != nullptr) {
        if (ent->d_name[0] != '.') {
            snprintf(path, sizeof(path), "%s/logs/%s", MOUNT_POINT, ent->d_name);
            if (strcmp(path, m_active_log_path) != 0) {
                unlink(path);
            }
        }
    }
    closedir(dir);
    return true;
}

size_t SDCardManager::getIconsCount() const {
    if (!m_mounted) return 0;
    DIR* dir = opendir(MOUNT_POINT "/icons");
    if (!dir) return 0;
    size_t count = 0;
    struct dirent* ent;
    while ((ent = readdir(dir)) != nullptr) {
        if (ent->d_name[0] != '.') {
            count++;
        }
    }
    closedir(dir);
    return count;
}

bool SDCardManager::runBenchmark(float* out_write_speed_mbs, float* out_read_speed_mbs) {
    if (!m_mounted || !out_write_speed_mbs || !out_read_speed_mbs) return false;

    constexpr size_t BUF_SIZE = 32 * 1024;
    constexpr size_t TOTAL_TEST_BYTES = 512 * 1024;
    constexpr size_t CHUNKS = TOTAL_TEST_BYTES / BUF_SIZE;

    uint8_t* buf = (uint8_t*)malloc(BUF_SIZE);
    if (!buf) {
        ESP_LOGE(TAG, "Impossibile allocare buffer benchmark");
        return false;
    }
    memset(buf, 0x5A, BUF_SIZE);

    const char* test_file = MOUNT_POINT "/.benchmark.tmp";

    // 1. Scrittura
    int64_t t_w_start = esp_timer_get_time();
    FILE* f = fopen(test_file, "wb");
    if (!f) {
        free(buf);
        return false;
    }
    for (size_t i = 0; i < CHUNKS; ++i) {
        if (fwrite(buf, 1, BUF_SIZE, f) != BUF_SIZE) {
            fclose(f);
            unlink(test_file);
            free(buf);
            return false;
        }
    }
    fflush(f);
    fclose(f);
    int64_t t_w_end = esp_timer_get_time();
    float w_sec = (float)(t_w_end - t_w_start) / 1000000.0f;
    *out_write_speed_mbs = (w_sec > 0.0001f) ? ((float)TOTAL_TEST_BYTES / (1024.0f * 1024.0f) / w_sec) : 0.0f;

    // 2. Lettura
    int64_t t_r_start = esp_timer_get_time();
    f = fopen(test_file, "rb");
    if (!f) {
        unlink(test_file);
        free(buf);
        return false;
    }
    for (size_t i = 0; i < CHUNKS; ++i) {
        if (fread(buf, 1, BUF_SIZE, f) != BUF_SIZE) {
            fclose(f);
            unlink(test_file);
            free(buf);
            return false;
        }
    }
    fclose(f);
    int64_t t_r_end = esp_timer_get_time();
    float r_sec = (float)(t_r_end - t_r_start) / 1000000.0f;
    *out_read_speed_mbs = (r_sec > 0.0001f) ? ((float)TOTAL_TEST_BYTES / (1024.0f * 1024.0f) / r_sec) : 0.0f;

    unlink(test_file);
    free(buf);
    ESP_LOGI(TAG, "Benchmark MicroSD completato: Scrittura=%.2f MB/s, Lettura=%.2f MB/s",
             *out_write_speed_mbs, *out_read_speed_mbs);
    return true;
}

bool SDCardManager::remount() {
    if (m_mounted && s_card) {
        esp_vfs_fat_sdcard_unmount(MOUNT_POINT, s_card);
        s_card = nullptr;
        m_mounted = false;
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    return init();
}

} // namespace abarth::storage
