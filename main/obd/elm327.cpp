#include "obd/elm327.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>

#include <esp_log.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace abarth::obd {

namespace {
constexpr const char* TAG = "elm327";

std::string strip(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c == '\r' || c == '\n' || c == ' ' || c == '\t') continue;
        out.push_back(c);
    }
    return out;
}

std::string upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::toupper(c); });
    return s;
}
}  // namespace

Elm327::Elm327(IStream* stream) : stream_(stream) {}

ElmResponse Elm327::command(const std::string& cmd, uint32_t timeout_ms) {
    ElmResponse out;
    if (!stream_ || !stream_->isConnected()) {
        out.error = "stream non connesso";
        return out;
    }
    stream_->flushRx();
    std::string tx = cmd + "\r";
    stream_->write(tx.c_str());

    char buf[512];
    const size_t n = stream_->readUntil('>', buf, sizeof(buf), timeout_ms);
    if (n == 0) {
        out.error = "timeout";
        return out;
    }
    out.raw.assign(buf, n);
    const std::string payload = upper(strip(out.raw));

    // NO DATA / 7F: l'ECU ha risposto, ma non ha il PID richiesto.
    // E' un "errore di protocollo", NON un errore di collegamento: il link
    // BLE e' sano, semplicemente la centralina non supporta quel PID.
    if (payload.find("NODATA") != std::string::npos ||
        payload.find("7F") != std::string::npos) {
        out.error   = "NO DATA";
        out.no_data = true;
        return out;
    }
    if (payload.find("?") != std::string::npos ||
        payload.find("UNABLETOCONNECT") != std::string::npos ||
        payload.find("ERROR") != std::string::npos ||
        payload.find("BUSBUSY") != std::string::npos ||
        payload.find("CANERROR") != std::string::npos ||
        payload.find("STOPPED") != std::string::npos ||
        payload.find("DATAERROR") != std::string::npos ||
        payload.find("BUFFERFULL") != std::string::npos) {
        out.error = payload;
        return out;
    }
    out.ok = true;
    return out;
}

bool Elm327::init(uint32_t timeout_ms) {
    // Reset + protocollo automatico. Sequenza ottimizzata per vLinker e cloni ELM327.
    // NOTA: tra un comando AT e l'altro lasciamo un piccolo ritardo perché molti
    // cloni cinesi su BLE (con MTU limitato) non reggono comandi in raffica e
    // cominciano a rispondere "?" o "STOPPED".
    const char* seq[] = {
        "ATZ",        // reset
        "ATE0",       // echo off
        "ATL0",       // linefeeds off
        "ATS0",       // spaces off
        "ATH0",       // headers off
        "ATSP0",      // auto protocol
        "ATAT1",      // adaptive timing 1 (il dongle ritrasmette in autonomia)
    };
    for (const char* c : seq) {
        ElmResponse r = command(c, timeout_ms);
        if (!r.ok) {
            ESP_LOGW(TAG, "init: comando %s fallito (%s)", c, r.error.c_str());
            // ATZ risponde con banner, potrebbe contenere caratteri non ascii.
            if (std::strcmp(c, "ATZ") != 0) return false;
        }
        if (std::strcmp(c, "ATZ") == 0) {
            // Dopo il reset il dongle impiega tempo a ristabilizzarsi.
            vTaskDelay(pdMS_TO_TICKS(800));
            if (stream_) stream_->flushRx();
        } else {
            // Pausa tra comandi AT: evita overflow sul dongle BLE.
            vTaskDelay(pdMS_TO_TICKS(60));
        }
    }
    // Esegue una prima richiesta PID 0100 per forzare la sincronizzazione sul bus CAN della vettura.
    // Ignoriamo il risultato: alcuni cloni rispondono NO DATA alla prima query
    // mentre negoziano il protocollo; va bene cosi'.
    command("0100", 5000);
    vTaskDelay(pdMS_TO_TICKS(200));
    if (stream_) stream_->flushRx();
    return true;
}

float Elm327::readBatteryVoltage(uint32_t timeout_ms) {
    ElmResponse r = command("ATRV", timeout_ms);
    if (!r.ok) return std::nanf("");
    // esempio risposta: "12.4V"
    float v = 0.0f;
    if (std::sscanf(r.raw.c_str(), " %fV", &v) == 1) return v;
    // fallback: cerca il numero
    const char* s = r.raw.c_str();
    while (*s && (*s < '0' || *s > '9') && *s != '.') ++s;
    if (std::sscanf(s, "%f", &v) == 1) return v;
    return std::nanf("");
}

}  // namespace abarth::obd
