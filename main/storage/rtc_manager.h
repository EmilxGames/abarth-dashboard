#pragma once

#include <cstdint>
#include <ctime>
#include <string>

namespace abarth::storage {

enum class RtcChipType {
    NONE = 0,
    SD2405,   // SD2405 / SD3031 (Guition JC1060P470C nativo su 0x32, 16 registri 0x00..0x0F)
    RX8130,   // Epson RX-8130CE
    PCF8563,  // NXP PCF8563
    DS3231,   // Maxim DS3231 / DS1307
    GENERIC
};

class RtcManager {
public:
    static RtcManager& instance();

    /**
     * @brief Inizializza il Real-Time Clock hardware.
     *        Scansiona il bus I2C per trovare il chip RTC (RX8130CE su 0x32),
     *        verifica lo stato della batteria tampone CR1220, legge data e ora
     *        e aggiorna il clock di sistema ESP32 (settimeofday).
     * @return true se il chip RTC e' stato trovato e inizializzato.
     */
    bool init();

    /**
     * @brief Verifica se un chip RTC hardware e' presente e funzionante.
     */
    bool isAvailable() const { return m_chip_type != RtcChipType::NONE; }

    /**
     * @brief Restituisce il tipo di chip rilevato.
     */
    RtcChipType getChipType() const { return m_chip_type; }

    /**
     * @brief Restituisce il nome descrittivo del chip RTC.
     */
    const char* getChipName() const;

    /**
     * @brief Restituisce l'indirizzo I2C 7-bit su cui risponde l'RTC.
     */
    uint8_t getI2cAddress() const { return m_i2c_addr; }

    /**
     * @brief Verifica se la batteria tampone (CR1220) e' carica e presente.
     *        Controlla il flag hardware di caduta tensione (VLF).
     */
    bool isBatteryOk() const { return m_battery_ok; }

    /**
     * @brief Verifica se l'orario nell'RTC e' valido (anno >= 2024).
     */
    bool isTimeValid() const { return m_time_valid; }

    /**
     * @brief Legge direttamente l'orario dai registri hardware dell'RTC.
     */
    bool readHardwareTime(struct tm* out_tm);

    /**
     * @brief Scrive un nuovo orario nei registri hardware dell'RTC
     *        e sincronizza il clock di sistema ESP32.
     */
    bool writeHardwareTime(const struct tm* in_tm);

    /**
     * @brief Sincronizza l'RTC con l'ora corrente del sistema ESP32.
     */
    bool syncRtcFromSystem();

    /**
     * @brief Imposta l'RTC all'orario di compilazione del firmware.
     */
    bool setTimeFromCompile();

    /**
     * @brief Regola l'orario di un certo numero di ore (+1 / -1).
     */
    bool adjustHours(int delta_hours);

    /**
     * @brief Regola l'orario di un certo numero di minuti (+1 / -1 / +5 / -5).
     */
    bool adjustMinutes(int delta_minutes);

    /**
     * @brief Azzera i secondi a :00 (comodo per sincronizzazione orologio).
     */
    bool resetSeconds();

    /**
     * @brief Regola il calendario di un certo numero di giorni (+1 / -1).
     */
    bool adjustDays(int delta_days);

    /**
     * @brief Regola il calendario di un certo numero di mesi (+1 / -1).
     */
    bool adjustMonths(int delta_months);

    /**
     * @brief Regola il calendario di un certo numero di anni (+1 / -1).
     */
    bool adjustYears(int delta_years);

    /**
     * @brief Imposta direttamente data e ora complete.
     */
    bool setDateTime(int year, int mon, int day, int hour, int min, int sec);

    /**
     * @brief Imposta direttamente l'orario da un timestamp UNIX epoch.
     */
    bool setEpoch(time_t epoch_sec);

    /**
     * @brief Abilita la sincronizzazione automatica SNTP via WiFi.
     */
    void enableNtpSync();

    /**
     * @brief Callback invocato quando il client SNTP aggancia l'ora atomica da internet.
     */
    void onNtpTimeSynced();

    /**
     * @brief Verifica se e' avvenuta una sincronizzazione NTP in questa sessione.
     */
    bool isNtpSynced() const { return m_ntp_synced; }

    /**
     * @brief Restituisce una stringa formattata con data e ora (es. "18/10/2026 21:30:00").
     */
    std::string getFormattedTime(const char* fmt = "%d/%m/%Y %H:%M:%S") const;

    /**
     * @brief Restituisce solo l'ora formattata (es. "21:30").
     */
    std::string getFormattedClock() const;

private:
    RtcManager() = default;
    ~RtcManager() = default;

    RtcManager(const RtcManager&) = delete;
    RtcManager& operator=(const RtcManager&) = delete;

    bool scanAndDetect();
    bool probeAddress(uint8_t addr);

    bool readSd2405(struct tm* out_tm);
    bool writeSd2405(const struct tm* in_tm);

    bool readRx8130(struct tm* out_tm);
    bool writeRx8130(const struct tm* in_tm);

    bool readPcf8563(struct tm* out_tm);
    bool writePcf8563(const struct tm* in_tm);

    bool readDs3231(struct tm* out_tm);
    bool writeDs3231(const struct tm* in_tm);

    RtcChipType m_chip_type   = RtcChipType::NONE;
    uint8_t     m_i2c_addr    = 0;
    bool        m_battery_ok  = false;
    bool        m_time_valid  = false;
    bool        m_ntp_synced  = false;
    bool        m_ntp_enabled = false;
};

inline RtcManager& rtc() {
    return RtcManager::instance();
}

} // namespace abarth::storage
