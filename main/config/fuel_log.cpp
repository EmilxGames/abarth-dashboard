#include "config/fuel_log.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <mutex>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <nvs.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "config/notify_log.h"
#include "config/settings.h"
#include "data/data_store.h"
#include "storage/sd_card_manager.h"

namespace abarth::fuel {
namespace {
constexpr const char* TAG = "rifornimenti";
constexpr uint32_t kMagic = 0x52464C31;   // "RFL1"
constexpr int kNvsKeep = 10;
constexpr int kWin = 21;                  // campioni per la mediana (~21 s)

struct Ring {
    uint32_t magic;
    uint16_t head, count;
    Refuel e[kMax];
};

// Stato persistente (NVS) per registro: livello stabile, km e ultimo prezzo
struct Persist {
    uint16_t stable_x10 = 0xFFFF;   ///< ultimo livello stabile x10 (0xFFFF = sconosciuto)
    uint32_t km_x10 = 0;            ///< km percorsi dall'ultimo rifornimento
    uint32_t price_milli = 0;
};

std::mutex g_mx;
Ring* g_ring[2] = {nullptr, nullptr};   // 0 reali, 1 simulati
Persist g_p[2];
std::atomic<uint8_t> g_dirty{0};        // bit0/1 registro, bit2/3 stato
float g_win[kWin];
int g_wn = 0, g_wi = 0;
int64_t g_last_sample_ms = 0;
float g_level = -1;                      // mediana attuale
double g_km_acc = 0;
volatile float g_override = -1;          // solo test

int cur() { return settings::current().demo_mode ? 1 : 0; }
const char* file_of(int k) { return k ? "/sdcard/rifornimenti_sim.bin" : "/sdcard/rifornimenti.bin"; }
const char* nvs_key(int k) { return k ? "rf_sim" : "rf_real"; }
const char* nvs_pkey(int k) { return k ? "rfp_sim" : "rfp_real"; }

void reset_ring(Ring* r) { std::memset(static_cast<void*>(r), 0, sizeof(Ring)); r->magic = kMagic; }

void load(int k) {
    Ring* r = g_ring[k];
    bool ok = false;
    if (storage::sd().isMounted()) {
        if (FILE* f = std::fopen(file_of(k), "rb")) {
            ok = std::fread(r, 1, sizeof(Ring), f) == sizeof(Ring) && r->magic == kMagic && r->count <= kMax && r->head < kMax;
            std::fclose(f);
        }
    }
    nvs_handle_t h;
    if (nvs_open("abarth_fuel", NVS_READONLY, &h) == ESP_OK) {
        if (!ok) {
            reset_ring(r);
            Refuel* tmp = static_cast<Refuel*>(heap_caps_malloc(kNvsKeep * sizeof(Refuel), MALLOC_CAP_SPIRAM));
            size_t sz = kNvsKeep * sizeof(Refuel);
            if (tmp && nvs_get_blob(h, nvs_key(k), tmp, &sz) == ESP_OK) {
                for (size_t i = 0; i < sz / sizeof(Refuel); ++i) {
                    r->e[r->head] = tmp[i];
                    r->head = (r->head + 1) % kMax;
                    if (r->count < kMax) ++r->count;
                }
            }
            heap_caps_free(tmp);
        }
        size_t ps = sizeof(Persist);
        if (nvs_get_blob(h, nvs_pkey(k), &g_p[k], &ps) != ESP_OK || ps != sizeof(Persist)) g_p[k] = Persist{};
        nvs_close(h);
    } else if (!ok) {
        reset_ring(r);
    }
}

void save(int k, bool ring) {
    if (ring) {
        Ring* copy = static_cast<Ring*>(heap_caps_malloc(sizeof(Ring), MALLOC_CAP_SPIRAM));
        if (!copy) return;
        { std::lock_guard<std::mutex> l(g_mx); std::memcpy(copy, g_ring[k], sizeof(Ring)); }
        bool done = false;
        if (storage::sd().isMounted()) {
            if (FILE* f = std::fopen(file_of(k), "wb")) {
                done = std::fwrite(copy, 1, sizeof(Ring), f) == sizeof(Ring);
                std::fclose(f);
            }
        }
        if (!done) {
            Refuel* tmp = static_cast<Refuel*>(heap_caps_malloc(kNvsKeep * sizeof(Refuel), MALLOC_CAP_SPIRAM));
            if (tmp) {
                const int n = std::min<int>(copy->count, kNvsKeep);
                for (int i = 0; i < n; ++i) tmp[i] = copy->e[(copy->head + kMax - n + i) % kMax];
                nvs_handle_t h;
                if (nvs_open("abarth_fuel", NVS_READWRITE, &h) == ESP_OK) {
                    nvs_set_blob(h, nvs_key(k), tmp, n * sizeof(Refuel));
                    nvs_commit(h);
                    nvs_close(h);
                }
                heap_caps_free(tmp);
            }
        }
        heap_caps_free(copy);
    }
    Persist p;
    { std::lock_guard<std::mutex> l(g_mx); p = g_p[k]; }
    nvs_handle_t h;
    if (nvs_open("abarth_fuel", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_blob(h, nvs_pkey(k), &p, sizeof(p));
        nvs_commit(h);
        nvs_close(h);
    }
}

void push_locked(int k, const Refuel& e) {
    Ring* r = g_ring[k];
    r->e[r->head] = e;
    r->head = (r->head + 1) % kMax;
    if (r->count < kMax) ++r->count;
}

uint32_t now_epoch() {
    const time_t t = time(nullptr);
    struct tm lt;
    localtime_r(&t, &lt);
    return lt.tm_year + 1900 >= 2024 ? static_cast<uint32_t>(t) : 0;
}

/// Un passo al secondo: km dalla velocita', livello filtrato, rilevamento
void step() {
    const int k = cur();
    const int64_t now = esp_timer_get_time() / 1000;
    auto& ds = data::store();
    // km: velocita' OBD integrata (solo con dato fresco)
    const auto sp = ds.get(data::Metric::Speed);
    if (sp.valid() && now - sp.updated_ms < 3000 && sp.value > 0.5f) {
        g_km_acc += sp.value / 3600.0;
        if (g_km_acc >= 0.1) {
            const uint32_t add = static_cast<uint32_t>(g_km_acc * 10);
            g_km_acc -= add / 10.0;
            std::lock_guard<std::mutex> l(g_mx);
            g_p[k].km_x10 += add;
        }
    }
    // livello: mediana degli ultimi ~21 campioni freschi (il galleggiante oscilla)
    auto lv = ds.get(data::Metric::FuelLevelPct);
    if (g_override >= 0) { lv.value = g_override; lv.updated_ms = now; }
    if (!(lv.valid() && now - lv.updated_ms < 5000 && lv.value >= 0 && lv.value <= 100)) {
        if (now - g_last_sample_ms > 120000) { g_wn = 0; g_level = -1; }   // dati assenti: si riparte
        return;
    }
    g_last_sample_ms = now;
    g_win[g_wi] = lv.value;
    g_wi = (g_wi + 1) % kWin;
    if (g_wn < kWin) ++g_wn;
    if (g_wn < kWin) return;   // servono abbastanza campioni
    float tmp[kWin];
    std::memcpy(tmp, g_win, sizeof(tmp));
    std::nth_element(tmp, tmp + kWin / 2, tmp + kWin);
    g_level = tmp[kWin / 2];
    const uint16_t lv10 = static_cast<uint16_t>(g_level * 10 + 0.5f);
    const auto& s = settings::current();
    bool refuel = false;
    Refuel e;
    {
        std::lock_guard<std::mutex> l(g_mx);
        Persist& p = g_p[k];
        if (p.stable_x10 == 0xFFFF) {
            p.stable_x10 = lv10;
            g_dirty.fetch_or(4 << k);
        } else if (lv10 >= p.stable_x10 + s.refuel_threshold_pct * 10) {
            // salito di almeno la soglia: rifornimento
            refuel = true;
            e.epoch = now_epoch();
            e.level_before = static_cast<uint8_t>(p.stable_x10 / 10);
            e.level_after = static_cast<uint8_t>(lv10 / 10);
            const float liters = (lv10 - p.stable_x10) / 1000.0f * s.tank_liters;
            e.liters_x10 = static_cast<uint16_t>(liters * 10 + 0.5f);
            e.price_milli = p.price_milli;
            e.km_x10 = p.km_x10;
            push_locked(k, e);
            p.km_x10 = 0;
            p.stable_x10 = lv10;
            g_dirty.fetch_or((1 << k) | (4 << k));
        } else if (lv10 < p.stable_x10) {
            // il livello scende: nuovo livello stabile (le piccole risalite si ignorano)
            p.stable_x10 = lv10;
            static int64_t last_save = 0;
            if (now - last_save > 60000) { last_save = now; g_dirty.fetch_or(4 << k); }
        }
    }
    if (refuel) {
        char m[160];
        std::snprintf(m, sizeof(m), "Circa %.1f L (da %u%% a %u%%). Correggi o aggiungi il prezzo: Impostazioni > Rifornimenti.",
                      e.liters_x10 / 10.0f, e.level_before, e.level_after);
        notify::add(0, "Rifornimento rilevato", m);
        ESP_LOGI(TAG, "rifornimento: %.1f L (%u%% -> %u%%), %.1f km dal precedente", e.liters_x10 / 10.0f,
                 e.level_before, e.level_after, e.km_x10 / 10.0f);
    }
}

void task(void*) {
    int n = 0;
    for (;;) {
        step();
        // salvataggi fuori dai calcoli; lo stato dei km ogni 5 minuti
        if (++n % 300 == 0) g_dirty.fetch_or(4 << cur());
        const uint8_t d = g_dirty.exchange(0);
        for (int k = 0; k < 2; ++k) if (d & ((1 | 4) << k)) save(k, d & (1 << k));
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
}  // namespace

void start() {
    {
        std::lock_guard<std::mutex> l(g_mx);
        if (g_ring[0]) return;
        for (int k = 0; k < 2; ++k) {
            g_ring[k] = static_cast<Ring*>(heap_caps_malloc(sizeof(Ring), MALLOC_CAP_SPIRAM));
            if (!g_ring[k]) return;
            reset_ring(g_ring[k]);
            load(k);
        }
    }
    xTaskCreate(task, "rifornimenti", 4096, nullptr, 1, nullptr);
    ESP_LOGI(TAG, "%u rifornimenti reali, %u simulati", g_ring[0]->count, g_ring[1]->count);
}

std::vector<Refuel> list() {
    std::vector<Refuel> v;
    std::lock_guard<std::mutex> l(g_mx);
    Ring* r = g_ring[cur()];
    if (!r) return v;
    for (int i = 1; i <= r->count; ++i) v.push_back(r->e[(r->head + kMax - i) % kMax]);
    return v;
}

namespace {
int slot_of(Ring* r, int idx) { return (r->head + kMax - 1 - idx) % kMax; }
}

void update(int idx, const Refuel& e) {
    const int k = cur();
    {
        std::lock_guard<std::mutex> l(g_mx);
        Ring* r = g_ring[k];
        if (!r || idx < 0 || idx >= r->count) return;
        r->e[slot_of(r, idx)] = e;
        if (e.price_milli) g_p[k].price_milli = e.price_milli;   // ricorda l'ultimo prezzo
    }
    g_dirty.fetch_or((1 | 4) << k);
}

void remove(int idx) {
    const int k = cur();
    {
        std::lock_guard<std::mutex> l(g_mx);
        Ring* r = g_ring[k];
        if (!r || idx < 0 || idx >= r->count) return;
        // ricompatta: le voci piu' recenti scalano di uno
        for (int i = idx; i > 0; --i) r->e[slot_of(r, i)] = r->e[slot_of(r, i - 1)];
        r->head = (r->head + kMax - 1) % kMax;
        --r->count;
    }
    g_dirty.fetch_or(1 << k);
}

int addManual(float liters) {
    const int k = cur();
    Refuel e;
    e.epoch = now_epoch();
    e.liters_x10 = static_cast<uint16_t>(std::max(0.0f, liters) * 10 + 0.5f);
    e.manual = 1;
    {
        std::lock_guard<std::mutex> l(g_mx);
        const float lvl = g_level >= 0 ? g_level : -1;
        const float tank = settings::current().tank_liters;
        e.level_after = lvl >= 0 ? static_cast<uint8_t>(lvl) : 0;
        e.level_before = lvl >= 0 ? static_cast<uint8_t>(std::max(0.0f, lvl - liters / tank * 100)) : 0;
        e.price_milli = g_p[k].price_milli;
        e.km_x10 = g_p[k].km_x10;
        g_p[k].km_x10 = 0;
        if (lvl >= 0) g_p[k].stable_x10 = static_cast<uint16_t>(lvl * 10);
        push_locked(k, e);
    }
    g_dirty.fetch_or((1 | 4) << k);
    return 0;
}

void debugOverrideLevel(float pct) { g_override = pct; }

uint32_t lastPriceMilli() {
    std::lock_guard<std::mutex> l(g_mx);
    return g_p[cur()].price_milli;
}

Stats stats() {
    Stats st;
    const auto v = list();
    const auto& s = settings::current();
    float km_sum = 0, l_sum = 0, eur_sum = 0, km_for_cost = 0;
    {
        std::lock_guard<std::mutex> l(g_mx);
        st.level_pct = g_level;
        st.km_since_last = g_p[cur()].km_x10 / 10.0f;
    }
    // consumo tra due rifornimenti: carburante usato = (livello dopo il precedente
    // - livello prima di questo) x capacita'; km = quelli registrati in questo
    for (size_t i = 0; i + 1 < v.size(); ++i) {
        const Refuel& now = v[i];
        const Refuel& prev = v[i + 1];
        const float km = now.km_x10 / 10.0f;
        if (km < 20 || prev.level_after <= now.level_before) continue;
        const float used = (prev.level_after - now.level_before) / 100.0f * s.tank_liters;
        km_sum += km;
        l_sum += used;
        if (prev.price_milli) { eur_sum += used * prev.price_milli / 1000.0f; km_for_cost += km; }
    }
    if (km_sum > 0 && l_sum > 0) {
        st.has_consumption = true;
        st.l_per_100km = l_sum / km_sum * 100.0f;
        st.km_per_l = km_sum / l_sum;
        if (st.level_pct >= 0) {
            st.has_range = true;
            st.range_km = st.level_pct / 100.0f * s.tank_liters / st.l_per_100km * 100.0f;
        }
    }
    if (km_for_cost > 0) { st.has_cost = true; st.eur_per_km = eur_sum / km_for_cost; }
    // spesa del mese
    const time_t t = time(nullptr);
    struct tm a;
    localtime_r(&t, &a);
    for (const auto& e : v) {
        if (!e.epoch || !e.price_milli) continue;
        const time_t et = e.epoch;
        struct tm b;
        localtime_r(&et, &b);
        if (a.tm_year == b.tm_year && a.tm_mon == b.tm_mon) st.month_eur += e.liters_x10 / 10.0f * e.price_milli / 1000.0f;
    }
    return st;
}

}  // namespace abarth::fuel
