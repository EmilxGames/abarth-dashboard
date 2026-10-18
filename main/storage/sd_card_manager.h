#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

namespace abarth::storage {

class SDCardManager {
public:
    static SDCardManager& instance();

    /**
     * @brief Inizializza l'interfaccia SDMMC Slot 0 (GPIO 39-44 a 4-bit)
     *        e monta il filesystem FatFS su /sdcard.
     *        Se la scheda non e' presente, fallisce graziosamente senza bloccare il sistema.
     * @return true se montata correttamente, false altrimenti.
     */
    bool init();

    /**
     * @brief Verifica se la scheda MicroSD e' attualmente montata e accessibile.
     */
    bool isMounted() const { return m_mounted; }

    /**
     * @brief Restituisce la dimensione totale della scheda in Megabyte.
     */
    uint64_t getTotalSizeMB() const { return m_total_size_mb; }

    /**
     * @brief Restituisce lo spazio libero della scheda in Megabyte.
     */
    uint64_t getFreeSizeMB() const;

    /**
     * @brief Restituisce lo spazio occupato della scheda in Megabyte.
     */
    uint64_t getUsedSizeMB() const {
        uint64_t free_mb = getFreeSizeMB();
        return (m_total_size_mb > free_mb) ? (m_total_size_mb - free_mb) : 0;
    }

    /**
     * @brief Restituisce il percorso del file log attivo.
     */
    const char* getActiveLogPath() const { return m_active_log_path; }

    /**
     * @brief Restituisce il numero di file di log presenti in /sdcard/logs.
     */
    size_t getLogFileCount() const;

    /**
     * @brief Restituisce la dimensione complessiva dei file di log in byte.
     */
    uint64_t getLogFilesTotalBytes() const;

    /**
     * @brief Avvia una nuova sessione di log telemetria con timestamp corrente.
     */
    bool startNewLogSession();

    /**
     * @brief Elimina tutti i vecchi file di log in /sdcard/logs.
     */
    bool clearLogs();

    /**
     * @brief Restituisce il numero di icone presenti in /sdcard/icons.
     */
    size_t getIconsCount() const;

    /**
     * @brief Esegue un test di lettura e scrittura da 512 KB per misurare il throughput (MB/s).
     */
    bool runBenchmark(float* out_write_speed_mbs, float* out_read_speed_mbs);

    /**
     * @brief Tenta di rimontare la scheda MicroSD (se inserita a caldo).
     */
    bool remount();

    /**
     * @brief Registra il driver filesystem LVGL associato alla lettera 'S'.
     *        Permette a LVGL di caricare immagini direttamente come "S:/icons/...".
     */
    void registerLvglFsDriver();

    /**
     * @brief Registra una riga di telemetria nel log CSV corrente su MicroSD.
     * @param csv_line Riga di testo CSV (senza o con newline)
     * @return true se scritta con successo, false altrimenti.
     */
    bool logTelemetry(const char* csv_line);

private:
    SDCardManager() = default;
    ~SDCardManager() = default;

    SDCardManager(const SDCardManager&) = delete;
    SDCardManager& operator=(const SDCardManager&) = delete;

    bool m_mounted = false;
    uint64_t m_total_size_mb = 0;
    char m_active_log_path[128] = {0};
};

inline SDCardManager& sd() {
    return SDCardManager::instance();
}

} // namespace abarth::storage
