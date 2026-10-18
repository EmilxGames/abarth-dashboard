#include "storage/rtc_manager.h"

#include <esp_log.h>
#include <driver/i2c.h>
#include <driver/gpio.h>
#include <sys/time.h>
#include <time.h>
#include <cstring>
#include <cstdio>
#include <nvs.h>
#include <nvs_flash.h>
#include "esp_sntp.h"

namespace abarth::storage {

namespace {
constexpr const char* TAG = "rtc";

// Pin standard bus I2C display / touch / RTC su Guition JC1060P470C
constexpr i2c_port_t RTC_I2C_PORT = I2C_NUM_0;
constexpr gpio_num_t RTC_I2C_SDA  = GPIO_NUM_7;
constexpr gpio_num_t RTC_I2C_SCL  = GPIO_NUM_8;
constexpr uint32_t   RTC_I2C_FREQ = 400000;

inline uint8_t dec2bcd(uint8_t dec) {
    return ((dec / 10) << 4) | (dec % 10);
}

inline uint8_t bcd2dec(uint8_t bcd) {
    return ((bcd >> 4) * 10) + (bcd & 0x0F);
}

bool parseCompileDateTime(struct tm* tm_out) {
    if (!tm_out) return false;
    std::memset(tm_out, 0, sizeof(*tm_out));

    const char* months[] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };
    char m_str[8] = {0};
    int day = 0, year = 0;
    if (std::sscanf(__DATE__, "%3s %d %d", m_str, &day, &year) != 3) {
        return false;
    }
    int month = 0;
    for (int i = 0; i < 12; ++i) {
        if (std::strcmp(m_str, months[i]) == 0) {
            month = i;
            break;
        }
    }
    int hour = 0, min = 0, sec = 0;
    if (std::sscanf(__TIME__, "%d:%d:%d", &hour, &min, &sec) != 3) {
        return false;
    }
    tm_out->tm_year = year - 1900;
    tm_out->tm_mon  = month;
    tm_out->tm_mday = day;
    tm_out->tm_hour = hour;
    tm_out->tm_min  = min;
    tm_out->tm_sec  = sec;
    tm_out->tm_isdst = -1;
    return true;
}

void sntp_sync_notification_cb(struct timeval* tv) {
    ESP_LOGI("sntp", "Sincronizzazione NTP completata! Aggiornamento RTC hardware...");
    rtc().onNtpTimeSynced();
}

} // namespace

RtcManager& RtcManager::instance() {
    static RtcManager s_instance;
    return s_instance;
}

const char* RtcManager::getChipName() const {
    switch (m_chip_type) {
        case RtcChipType::SD2405:  return "SD2405 / SD3031";
        case RtcChipType::RX8130:  return "Epson RX-8130CE";
        case RtcChipType::PCF8563: return "NXP PCF8563";
        case RtcChipType::DS3231:  return "Maxim DS3231/DS1307";
        case RtcChipType::GENERIC: return "RTC I2C Generico";
        default:                   return "Non Rilevato";
    }
}

bool RtcManager::probeAddress(uint8_t addr) {
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(RTC_I2C_PORT, cmd, pdMS_TO_TICKS(15));
    i2c_cmd_link_delete(cmd);
    return (ret == ESP_OK);
}

bool RtcManager::scanAndDetect() {
    if (probeAddress(0x32)) {
        uint8_t test_buf[7] = {0};
        uint8_t reg0 = 0x00;
        esp_err_t err = i2c_master_write_read_device(RTC_I2C_PORT, 0x32, &reg0, 1, test_buf, 7, pdMS_TO_TICKS(50));
        if (err == ESP_OK) {
            m_chip_type = RtcChipType::SD2405;
            m_i2c_addr = 0x32;
            ESP_LOGI(TAG, "Trovato chip RTC SD2405 su 0x32! (Bytes: %02X %02X %02X %02X %02X %02X %02X)",
                     test_buf[0], test_buf[1], test_buf[2], test_buf[3], test_buf[4], test_buf[5], test_buf[6]);
            return true;
        }
    }

    m_chip_type = RtcChipType::NONE;
    m_i2c_addr = 0;
    return false;
}

bool RtcManager::init() {
    ESP_LOGI(TAG, "Inizializzazione Real-Time Clock (RTC)...");

    // Imposta il fuso orario standard italiano / europeo (CET/CEST) con ora solare/legale automatica
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
    tzset();

    if (!scanAndDetect()) {
        ESP_LOGW(TAG, "Nessun chip RTC hardware riconosciuto. Il sistema usera' il clock interno ESP32.");
        return false;
    }

    struct tm rtc_tm = {};
    if (readHardwareTime(&rtc_tm)) {
        int year = rtc_tm.tm_year + 1900;
        ESP_LOGI(TAG, "Lettura RTC riuscita: %02d/%02d/%04d %02d:%02d:%02d [Batteria CARICA (OK)]",
                 rtc_tm.tm_mday, rtc_tm.tm_mon + 1, year,
                 rtc_tm.tm_hour, rtc_tm.tm_min, rtc_tm.tm_sec);

        const bool valid_calendar = (year >= 2024 && year <= 2038) &&
                                   (rtc_tm.tm_mon >= 0 && rtc_tm.tm_mon <= 11) &&
                                   (rtc_tm.tm_mday >= 1 && rtc_tm.tm_mday <= 31) &&
                                   (rtc_tm.tm_hour >= 0 && rtc_tm.tm_hour <= 23) &&
                                   (rtc_tm.tm_min >= 0 && rtc_tm.tm_min <= 59) &&
                                   (rtc_tm.tm_sec >= 0 && rtc_tm.tm_sec <= 59);

        if (valid_calendar) {
            m_time_valid = true;
            time_t t = mktime(&rtc_tm);
            if (t > 0) {
                struct timeval tv = { .tv_sec = t, .tv_usec = 0 };
                settimeofday(&tv, nullptr);
                ESP_LOGI(TAG, "Clock di sistema ESP32 sincronizzato con successo da RTC hardware! (%02d/%02d/%04d %02d:%02d:%02d)",
                         rtc_tm.tm_mday, rtc_tm.tm_mon + 1, year, rtc_tm.tm_hour, rtc_tm.tm_min, rtc_tm.tm_sec);
            }
        } else {
            ESP_LOGW(TAG, "Orario RTC non valido o non inizializzato (<2024). Inizializzazione automatica con data build firmware...");
            setTimeFromCompile();
        }
    } else {
        ESP_LOGE(TAG, "Impossibile leggere i registri orario del chip RTC");
        return false;
    }

    return true;
}

bool RtcManager::readSd2405(struct tm* out_tm) {
    if (!out_tm) return false;

    uint8_t start_reg = 0x00;
    uint8_t buf[7] = {0};
    esp_err_t err = i2c_master_write_read_device(RTC_I2C_PORT, m_i2c_addr,
                                       &start_reg, 1, buf, sizeof(buf), pdMS_TO_TICKS(100));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Lettura registri SD2405 fallita (err=%s)", esp_err_to_name(err));
        return false;
    }

    out_tm->tm_sec  = bcd2dec(buf[0] & 0x7F);
    out_tm->tm_min  = bcd2dec(buf[1] & 0x7F);
    out_tm->tm_hour = bcd2dec(buf[2] & 0x3F);
    out_tm->tm_wday = buf[3] & 0x07;
    out_tm->tm_mday = bcd2dec(buf[4] & 0x3F);
    out_tm->tm_mon  = bcd2dec(buf[5] & 0x1F) - 1; // 0..11
    out_tm->tm_year = bcd2dec(buf[6]) + 100;      // Anni dal 1900
    out_tm->tm_isdst = -1;

    m_battery_ok = true;
    m_time_valid = true;
    return true;
}

bool RtcManager::writeSd2405(const struct tm* in_tm) {
    if (!in_tm) return false;

    uint8_t buf[8];
    buf[0] = 0x00;                                      // Registro iniziale 0x00
    buf[1] = dec2bcd(in_tm->tm_sec);                    // 0x00: Secondi (00..59)
    buf[2] = dec2bcd(in_tm->tm_min);                    // 0x01: Minuti (00..59)
    buf[3] = dec2bcd(in_tm->tm_hour) | 0x80;           // 0x02: Ore (Bit 7 = 1 per modalita' 24h)
    buf[4] = static_cast<uint8_t>(in_tm->tm_wday % 7);  // 0x03: Giorno settimana (0..6)
    buf[5] = dec2bcd(in_tm->tm_mday);                   // 0x04: Giorno del mese (01..31)
    buf[6] = dec2bcd(in_tm->tm_mon + 1);                // 0x05: Mese (01..12)
    buf[7] = dec2bcd((in_tm->tm_year + 1900) % 100);    // 0x06: Anno (00..99)

    esp_err_t err = i2c_master_write_to_device(RTC_I2C_PORT, m_i2c_addr, buf, sizeof(buf), pdMS_TO_TICKS(100));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SD2405 burst write orario fallita (err=%s)", esp_err_to_name(err));
        return false;
    }

    ESP_LOGI(TAG, "SD2405 scrittura orario riuscita! (%02d/%02d/%04d %02d:%02d:%02d)",
             in_tm->tm_mday, in_tm->tm_mon + 1, in_tm->tm_year + 1900,
             in_tm->tm_hour, in_tm->tm_min, in_tm->tm_sec);
    m_battery_ok = true;
    m_time_valid = true;
    return true;
}

bool RtcManager::readRx8130(struct tm* out_tm) {
    if (!out_tm) return false;

    // 1. Controllo registro FLAG (0x1D): Bit 1 = VLF (Voltage Low Flag)
    uint8_t flag_reg = 0x1D;
    uint8_t flag_val = 0;
    esp_err_t err = i2c_master_write_read_device(RTC_I2C_PORT, m_i2c_addr,
                                                &flag_reg, 1, &flag_val, 1, pdMS_TO_TICKS(50));
    if (err == ESP_OK) {
        m_battery_ok = ((flag_val & 0x02) == 0);
    } else {
        m_battery_ok = false;
    }

    // 2. Lettura 7 registri calendario (0x10 .. 0x16)
    uint8_t start_reg = 0x10;
    uint8_t buf[7] = {0};
    err = i2c_master_write_read_device(RTC_I2C_PORT, m_i2c_addr,
                                       &start_reg, 1, buf, sizeof(buf), pdMS_TO_TICKS(100));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Lettura registri RX8130 fallita (err=%s)", esp_err_to_name(err));
        return false;
    }

    out_tm->tm_sec  = bcd2dec(buf[0] & 0x7F);
    out_tm->tm_min  = bcd2dec(buf[1] & 0x7F);
    out_tm->tm_hour = bcd2dec(buf[2] & 0x3F);

    // Weekday: RX8130 usa bitmask
    uint8_t wmask = buf[3] & 0x7F;
    out_tm->tm_wday = 0;
    for (int i = 0; i < 7; ++i) {
        if (wmask & (1 << i)) {
            out_tm->tm_wday = i;
            break;
        }
    }

    out_tm->tm_mday = bcd2dec(buf[4] & 0x3F);
    out_tm->tm_mon  = bcd2dec(buf[5] & 0x1F) - 1; // 0..11
    out_tm->tm_year = bcd2dec(buf[6]) + 100;      // Anni dal 1900
    out_tm->tm_isdst = -1;

    return true;
}

bool RtcManager::writeRx8130(const struct tm* in_tm) {
    if (!in_tm) return false;

    // 0. Assicura commutazione automatica su batteria CR1220 (CTRL1 = 0x30)
    uint8_t ctrl1_cmd[2] = { 0x1F, 0x30 };
    i2c_master_write_to_device(RTC_I2C_PORT, m_i2c_addr, ctrl1_cmd, 2, pdMS_TO_TICKS(50));

    // 1. Leggi CTRL0 (0x1E) e imposta STOP (bit 6 = 0x40)
    uint8_t ctrl0_reg = 0x1E;
    uint8_t ctrl0_val = 0;
    i2c_master_write_read_device(RTC_I2C_PORT, m_i2c_addr, &ctrl0_reg, 1, &ctrl0_val, 1, pdMS_TO_TICKS(50));

    uint8_t write_ctrl0[2] = { 0x1E, static_cast<uint8_t>(ctrl0_val | 0x40) };
    i2c_master_write_to_device(RTC_I2C_PORT, m_i2c_addr, write_ctrl0, 2, pdMS_TO_TICKS(50));

    // 2. Scrivi registri 0x10..0x16
    uint8_t buf[8];
    buf[0] = 0x10;
    buf[1] = dec2bcd(in_tm->tm_sec);
    buf[2] = dec2bcd(in_tm->tm_min);
    buf[3] = dec2bcd(in_tm->tm_hour);
    buf[4] = static_cast<uint8_t>(1 << (in_tm->tm_wday % 7));
    buf[5] = dec2bcd(in_tm->tm_mday);
    buf[6] = dec2bcd(in_tm->tm_mon + 1);
    buf[7] = dec2bcd((in_tm->tm_year + 1900) % 100);

    esp_err_t err = i2c_master_write_to_device(RTC_I2C_PORT, m_i2c_addr, buf, sizeof(buf), pdMS_TO_TICKS(100));

    // 3. Ripristina CTRL0 azzerando STOP
    write_ctrl0[1] = static_cast<uint8_t>(ctrl0_val & ~0x40);
    i2c_master_write_to_device(RTC_I2C_PORT, m_i2c_addr, write_ctrl0, 2, pdMS_TO_TICKS(50));

    // 4. Azzera il flag VLF (bit 1) nel registro FLAG (0x1D)
    uint8_t flag_reg = 0x1D;
    uint8_t flag_val = 0;
    if (i2c_master_write_read_device(RTC_I2C_PORT, m_i2c_addr, &flag_reg, 1, &flag_val, 1, pdMS_TO_TICKS(50)) == ESP_OK) {
        uint8_t write_flag[2] = { 0x1D, static_cast<uint8_t>(flag_val & ~0x02) };
        i2c_master_write_to_device(RTC_I2C_PORT, m_i2c_addr, write_flag, 2, pdMS_TO_TICKS(50));
    }

    m_battery_ok = true;
    m_time_valid = true;
    return (err == ESP_OK);
}

bool RtcManager::readPcf8563(struct tm* out_tm) {
    if (!out_tm) return false;
    uint8_t start_reg = 0x02;
    uint8_t buf[7] = {0};
    esp_err_t err = i2c_master_write_read_device(RTC_I2C_PORT, m_i2c_addr,
                                                &start_reg, 1, buf, sizeof(buf), pdMS_TO_TICKS(100));
    if (err != ESP_OK) return false;

    // Bit 7 del registro secondi e' VL (Voltage Low)
    m_battery_ok = ((buf[0] & 0x80) == 0);

    out_tm->tm_sec  = bcd2dec(buf[0] & 0x7F);
    out_tm->tm_min  = bcd2dec(buf[1] & 0x7F);
    out_tm->tm_hour = bcd2dec(buf[2] & 0x3F);
    out_tm->tm_mday = bcd2dec(buf[3] & 0x3F);
    out_tm->tm_wday = buf[4] & 0x07;
    out_tm->tm_mon  = bcd2dec(buf[5] & 0x1F) - 1;
    out_tm->tm_year = bcd2dec(buf[6]) + 100;
    out_tm->tm_isdst = -1;

    return true;
}

bool RtcManager::writePcf8563(const struct tm* in_tm) {
    if (!in_tm) return false;
    uint8_t buf[8];
    buf[0] = 0x02;
    buf[1] = dec2bcd(in_tm->tm_sec);
    buf[2] = dec2bcd(in_tm->tm_min);
    buf[3] = dec2bcd(in_tm->tm_hour);
    buf[4] = dec2bcd(in_tm->tm_mday);
    buf[5] = in_tm->tm_wday % 7;
    buf[6] = dec2bcd(in_tm->tm_mon + 1);
    buf[7] = dec2bcd((in_tm->tm_year + 1900) % 100);

    esp_err_t err = i2c_master_write_to_device(RTC_I2C_PORT, m_i2c_addr, buf, sizeof(buf), pdMS_TO_TICKS(100));
    m_battery_ok = true;
    m_time_valid = true;
    return (err == ESP_OK);
}

bool RtcManager::readDs3231(struct tm* out_tm) {
    if (!out_tm) return false;
    uint8_t start_reg = 0x00;
    uint8_t buf[7] = {0};
    esp_err_t err = i2c_master_write_read_device(RTC_I2C_PORT, m_i2c_addr,
                                                &start_reg, 1, buf, sizeof(buf), pdMS_TO_TICKS(100));
    if (err != ESP_OK) return false;

    // Registro status 0x0F: Bit 7 e' OSF (Oscillator Stop Flag)
    uint8_t stat_reg = 0x0F;
    uint8_t stat_val = 0;
    if (i2c_master_write_read_device(RTC_I2C_PORT, m_i2c_addr, &stat_reg, 1, &stat_val, 1, pdMS_TO_TICKS(50)) == ESP_OK) {
        m_battery_ok = ((stat_val & 0x80) == 0);
    }

    out_tm->tm_sec  = bcd2dec(buf[0] & 0x7F);
    out_tm->tm_min  = bcd2dec(buf[1] & 0x7F);
    out_tm->tm_hour = bcd2dec(buf[2] & 0x3F);
    out_tm->tm_wday = (buf[3] & 0x07) - 1;
    out_tm->tm_mday = bcd2dec(buf[4] & 0x3F);
    out_tm->tm_mon  = bcd2dec(buf[5] & 0x1F) - 1;
    out_tm->tm_year = bcd2dec(buf[6]) + 100;
    out_tm->tm_isdst = -1;

    return true;
}

bool RtcManager::writeDs3231(const struct tm* in_tm) {
    if (!in_tm) return false;
    uint8_t buf[8];
    buf[0] = 0x00;
    buf[1] = dec2bcd(in_tm->tm_sec);
    buf[2] = dec2bcd(in_tm->tm_min);
    buf[3] = dec2bcd(in_tm->tm_hour);
    buf[4] = (in_tm->tm_wday % 7) + 1;
    buf[5] = dec2bcd(in_tm->tm_mday);
    buf[6] = dec2bcd(in_tm->tm_mon + 1);
    buf[7] = dec2bcd((in_tm->tm_year + 1900) % 100);

    esp_err_t err = i2c_master_write_to_device(RTC_I2C_PORT, m_i2c_addr, buf, sizeof(buf), pdMS_TO_TICKS(100));

    // Azzera bit OSF in registro 0x0F
    uint8_t stat_reg = 0x0F;
    uint8_t stat_val = 0;
    if (i2c_master_write_read_device(RTC_I2C_PORT, m_i2c_addr, &stat_reg, 1, &stat_val, 1, pdMS_TO_TICKS(50)) == ESP_OK) {
        uint8_t write_stat[2] = { 0x0F, static_cast<uint8_t>(stat_val & ~0x80) };
        i2c_master_write_to_device(RTC_I2C_PORT, m_i2c_addr, write_stat, 2, pdMS_TO_TICKS(50));
    }

    m_battery_ok = true;
    m_time_valid = true;
    return (err == ESP_OK);
}

bool RtcManager::readHardwareTime(struct tm* out_tm) {
    switch (m_chip_type) {
        case RtcChipType::SD2405:  return readSd2405(out_tm);
        case RtcChipType::RX8130:  return readRx8130(out_tm);
        case RtcChipType::PCF8563: return readPcf8563(out_tm);
        case RtcChipType::DS3231:  return readDs3231(out_tm);
        default:                   return false;
    }
}

bool RtcManager::writeHardwareTime(const struct tm* in_tm) {
    bool ok = false;
    switch (m_chip_type) {
        case RtcChipType::SD2405:  ok = writeSd2405(in_tm); break;
        case RtcChipType::RX8130:  ok = writeRx8130(in_tm); break;
        case RtcChipType::PCF8563: ok = writePcf8563(in_tm); break;
        case RtcChipType::DS3231:  ok = writeDs3231(in_tm); break;
        default:                   ok = false; break;
    }

    if (ok && in_tm) {
        struct tm copy_tm = *in_tm;
        time_t t = mktime(&copy_tm);
        if (t > 0) {
            struct timeval tv = { .tv_sec = t, .tv_usec = 0 };
            settimeofday(&tv, nullptr);
        }
    }
    return ok;
}

bool RtcManager::syncRtcFromSystem() {
    time_t now = time(nullptr);
    struct tm ti;
    localtime_r(&now, &ti);
    if (ti.tm_year + 1900 < 2024) {
        ESP_LOGW(TAG, "Ora di sistema non valida (<2024), salto scrittura su RTC hardware");
        return false;
    }
    bool ok = writeHardwareTime(&ti);
    if (ok) {
        ESP_LOGI(TAG, "Ora di sistema trascritta con successo nell'RTC! (%02d:%02d:%02d)",
                 ti.tm_hour, ti.tm_min, ti.tm_sec);
    }
    return ok;
}

bool RtcManager::setTimeFromCompile() {
    struct tm ti = {};
    if (!parseCompileDateTime(&ti)) {
        return false;
    }
    time_t t = mktime(&ti);
    if (t > 0) {
        struct timeval tv = { .tv_sec = t, .tv_usec = 0 };
        settimeofday(&tv, nullptr);
    }
    bool ok = writeHardwareTime(&ti);
    if (ok) {
        ESP_LOGI(TAG, "RTC impostato alla data/ora di compilazione del firmware: %02d/%02d/%04d %02d:%02d:%02d",
                 ti.tm_mday, ti.tm_mon + 1, ti.tm_year + 1900,
                 ti.tm_hour, ti.tm_min, ti.tm_sec);
    }
    return ok;
}

bool RtcManager::adjustHours(int delta_hours) {
    time_t now = time(nullptr);
    now += delta_hours * 3600;
    struct timeval tv = { .tv_sec = now, .tv_usec = 0 };
    settimeofday(&tv, nullptr);
    struct tm ti;
    localtime_r(&now, &ti);
    return writeHardwareTime(&ti);
}

bool RtcManager::adjustMinutes(int delta_minutes) {
    time_t now = time(nullptr);
    now += delta_minutes * 60;
    struct timeval tv = { .tv_sec = now, .tv_usec = 0 };
    settimeofday(&tv, nullptr);
    struct tm ti;
    localtime_r(&now, &ti);
    return writeHardwareTime(&ti);
}

bool RtcManager::resetSeconds() {
    time_t now = time(nullptr);
    struct tm ti;
    localtime_r(&now, &ti);
    now -= ti.tm_sec;
    struct timeval tv = { .tv_sec = now, .tv_usec = 0 };
    settimeofday(&tv, nullptr);
    localtime_r(&now, &ti);
    return writeHardwareTime(&ti);
}

bool RtcManager::adjustDays(int delta_days) {
    time_t now = time(nullptr);
    now += delta_days * 86400;
    struct timeval tv = { .tv_sec = now, .tv_usec = 0 };
    settimeofday(&tv, nullptr);
    struct tm ti;
    localtime_r(&now, &ti);
    return writeHardwareTime(&ti);
}

bool RtcManager::adjustMonths(int delta_months) {
    time_t now = time(nullptr);
    struct tm ti;
    localtime_r(&now, &ti);
    ti.tm_mon += delta_months;
    ti.tm_isdst = -1;
    time_t updated = mktime(&ti);
    if (updated > 0) {
        struct timeval tv = { .tv_sec = updated, .tv_usec = 0 };
        settimeofday(&tv, nullptr);
        localtime_r(&updated, &ti);
        return writeHardwareTime(&ti);
    }
    return false;
}

bool RtcManager::adjustYears(int delta_years) {
    time_t now = time(nullptr);
    struct tm ti;
    localtime_r(&now, &ti);
    ti.tm_year += delta_years;
    ti.tm_isdst = -1;
    time_t updated = mktime(&ti);
    if (updated > 0) {
        struct timeval tv = { .tv_sec = updated, .tv_usec = 0 };
        settimeofday(&tv, nullptr);
        localtime_r(&updated, &ti);
        return writeHardwareTime(&ti);
    }
    return false;
}

bool RtcManager::setDateTime(int year, int mon, int day, int hour, int min, int sec) {
    struct tm ti = {};
    ti.tm_year = year - 1900;
    ti.tm_mon  = mon - 1;
    ti.tm_mday = day;
    ti.tm_hour = hour;
    ti.tm_min  = min;
    ti.tm_sec  = sec;
    ti.tm_isdst = -1;
    time_t t = mktime(&ti);
    if (t > 0) {
        struct timeval tv = { .tv_sec = t, .tv_usec = 0 };
        settimeofday(&tv, nullptr);
        localtime_r(&t, &ti);
        return writeHardwareTime(&ti);
    }
    return false;
}

bool RtcManager::setEpoch(time_t epoch_sec) {
    struct timeval tv = { .tv_sec = epoch_sec, .tv_usec = 0 };
    settimeofday(&tv, nullptr);
    struct tm ti;
    localtime_r(&epoch_sec, &ti);
    return writeHardwareTime(&ti);
}

void RtcManager::enableNtpSync() {
    if (m_ntp_enabled) return;
    if (esp_sntp_enabled()) {
        m_ntp_enabled = true;
        return;
    }

    ESP_LOGI(TAG, "Inizializzazione client SNTP per sincronizzazione ora via WiFi...");
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_setservername(1, "time.google.com");
    esp_sntp_set_time_sync_notification_cb(sntp_sync_notification_cb);
    esp_sntp_init();
    m_ntp_enabled = true;
}

void RtcManager::onNtpTimeSynced() {
    m_ntp_synced = true;
    syncRtcFromSystem();
}

std::string RtcManager::getFormattedTime(const char* fmt) const {
    time_t now = time(nullptr);
    struct tm ti;
    localtime_r(&now, &ti);
    char buf[64];
    std::strftime(buf, sizeof(buf), fmt ? fmt : "%d/%m/%Y %H:%M:%S", &ti);
    return std::string(buf);
}

std::string RtcManager::getFormattedClock() const {
    time_t now = time(nullptr);
    struct tm ti;
    localtime_r(&now, &ti);
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%02d:%02d", ti.tm_hour, ti.tm_min);
    return std::string(buf);
}

} // namespace abarth::storage
