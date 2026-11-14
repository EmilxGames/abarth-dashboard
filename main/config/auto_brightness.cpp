#include "config/auto_brightness.h"

#include <cmath>
#include <ctime>
#include <esp_log.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "config/settings.h"
#include "display/board.h"

namespace abarth::brightness {
namespace {
constexpr const char* TAG = "luminosita";
constexpr float kPi = 3.14159265f;
volatile float s_cur = -1;            ///< luminosita' applicata (%)
volatile bool s_snap = true;          ///< prossimo passo senza sfumare
Status s_status;
bool s_day_hyst = true;               ///< giorno/notte con isteresi

/// Altezza del sole sull'orizzonte (gradi) a quest'ora UTC, dato luogo
float sun_elevation(time_t utc, float lat, float lon) {
    const double n = utc / 86400.0 + 2440587.5 - 2451545.0;   // giorni da J2000
    const double L = std::fmod(280.460 + 0.9856474 * n, 360.0);
    const double g = std::fmod(357.528 + 0.9856003 * n, 360.0) * kPi / 180.0;
    const double lam = (L + 1.915 * std::sin(g) + 0.020 * std::sin(2 * g)) * kPi / 180.0;
    const double eps = (23.439 - 0.0000004 * n) * kPi / 180.0;
    const double ra = std::atan2(std::cos(eps) * std::sin(lam), std::cos(lam));
    const double dec = std::asin(std::sin(eps) * std::sin(lam));
    const double gmst = std::fmod(18.697374558 + 24.06570982441908 * n, 24.0) * 15.0 * kPi / 180.0;
    const double ha = gmst + lon * kPi / 180.0 - ra;
    const double la = lat * kPi / 180.0;
    const double el = std::asin(std::sin(la) * std::sin(dec) + std::cos(la) * std::cos(dec) * std::cos(ha));
    return static_cast<float>(el * 180.0 / kPi);
}

/// Alba e tramonto di oggi in minuti locali (ricerca a passi di 5 minuti)
void sun_times(time_t now, float lat, float lon, int16_t& rise, int16_t& set) {
    struct tm lt;
    localtime_r(&now, &lt);
    const time_t midnight = now - (lt.tm_hour * 3600 + lt.tm_min * 60 + lt.tm_sec);
    rise = set = -1;
    float prev = sun_elevation(midnight, lat, lon);
    for (int m = 5; m <= 1440; m += 5) {
        const float e = sun_elevation(midnight + m * 60, lat, lon);
        if (prev < -0.83f && e >= -0.83f && rise < 0) rise = m;
        if (prev >= -0.83f && e < -0.83f) set = m;
        prev = e;
    }
}

void step() {
    const auto& s = settings::current();
    const time_t now = time(nullptr);
    struct tm lt;
    localtime_r(&now, &lt);
    const bool valid = lt.tm_year + 1900 >= 2024;
    Status st;
    st.time_valid = valid;
    st.auto_on = s.bright_mode != 0 && valid;
    float target = s.brightness;   // manuale (o automatica senza ora valida)
    if (st.auto_on) {
        bool day;
        if (s.bright_mode == 1) {
            // giorno se il sole e' sopra -3 gradi (con un grado di isteresi)
            const float el = sun_elevation(now, s.lat_x100 / 100.0f, s.lon_x100 / 100.0f);
            day = s_day_hyst ? el > -4.0f : el > -2.0f;
            if (lt.tm_sec == 0 || s_status.sunrise_min < 0)
                sun_times(now, s.lat_x100 / 100.0f, s.lon_x100 / 100.0f, st.sunrise_min, st.sunset_min);
            else { st.sunrise_min = s_status.sunrise_min; st.sunset_min = s_status.sunset_min; }
        } else {
            const int m = lt.tm_hour * 60 + lt.tm_min;
            const int d = s.day_from_min, nn = s.night_from_min;
            day = d < nn ? (m >= d && m < nn) : !(m >= nn && m < d);
        }
        s_day_hyst = day;
        st.is_day = day;
        target = day ? s.bright_day : s.bright_night;
    }
    if (s.bright_mode == 0) {   // manuale: il cursore applica gia' la luminosita'
        s_cur = s.brightness;
    } else if (s_snap || s_cur < 0) {
        s_cur = target;
        display::setBrightness(static_cast<int>(target + 0.5f));
    } else if (std::fabs(target - s_cur) >= 0.5f) {
        // passaggio graduale: circa 45 s per l'intera escursione
        const float stepv = 100.0f / 45.0f;
        s_cur = target > s_cur ? std::fmin(target, s_cur + stepv) : std::fmax(target, s_cur - stepv);
        display::setBrightness(static_cast<int>(s_cur + 0.5f));
    }
    s_snap = false;
    st.current = static_cast<uint8_t>(s_cur < 0 ? s.brightness : s_cur + 0.5f);
    s_status = st;
}

void task(void*) {
    for (;;) {
        step();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
}  // namespace

void start() {
    static bool started = false;
    if (started) return;
    started = true;
    xTaskCreate(task, "luminosita", 4096, nullptr, 1, nullptr);
    ESP_LOGI(TAG, "luminosita' automatica pronta (modalita' %u)", settings::current().bright_mode);
}

Status status() { return s_status; }

void refreshNow() { s_snap = true; }

}  // namespace abarth::brightness
