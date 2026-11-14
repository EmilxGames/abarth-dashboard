#include "config/notify_log.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <mutex>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <nvs.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "config/settings.h"
#include "storage/sd_card_manager.h"

namespace abarth::notify {
namespace {
constexpr const char* TAG = "notifiche";
constexpr uint32_t kMagic = 0x4E4F5431;   // "NOT1"
constexpr int kNvsKeep = 20;               // senza microSD: le ultime 20

struct Ring {
    uint32_t magic = kMagic;
    uint16_t head = 0;    ///< prossima posizione libera
    uint16_t count = 0;
    Entry e[kMax];
};

std::mutex g_mx;
Ring* g_ring[2] = {nullptr, nullptr};   // 0 reali, 1 simulati (in PSRAM)
std::atomic<uint32_t> g_rev{1};
std::atomic<uint8_t> g_dirty{0};        // bit per registro da salvare
TaskHandle_t g_io = nullptr;

/// azzera un registro senza temporanei sulla pila (il registro e' di ~32 KB)
void reset_ring(Ring* r) {
    std::memset(static_cast<void*>(r), 0, sizeof(Ring));
    r->magic = kMagic;
}

const char* file_of(int k) { return k ? "/sdcard/notifiche_sim.bin" : "/sdcard/notifiche.bin"; }
const char* nvs_key(int k) { return k ? "ntf_sim" : "ntf_real"; }

bool sd_ok() { return storage::sd().isMounted(); }

void load(int k) {
    Ring* r = g_ring[k];
    bool ok = false;
    if (sd_ok()) {
        if (FILE* f = std::fopen(file_of(k), "rb")) {
            ok = std::fread(r, 1, sizeof(Ring), f) == sizeof(Ring) && r->magic == kMagic && r->count <= kMax && r->head < kMax;
            std::fclose(f);
        }
    }
    if (!ok) {   // dalla NVS (ultime voci) o vuoto
        reset_ring(r);
        nvs_handle_t h;
        Entry* tmp = static_cast<Entry*>(heap_caps_malloc(kNvsKeep * sizeof(Entry), MALLOC_CAP_SPIRAM));
        if (tmp && nvs_open("abarth_ntf", NVS_READONLY, &h) == ESP_OK) {
            size_t sz = kNvsKeep * sizeof(Entry);
            if (nvs_get_blob(h, nvs_key(k), tmp, &sz) == ESP_OK) {
                const int n = static_cast<int>(sz / sizeof(Entry));
                for (int i = 0; i < n && i < kNvsKeep; ++i) {   // dalla piu' vecchia
                    r->e[r->head] = tmp[i];
                    r->head = (r->head + 1) % kMax;
                    if (r->count < kMax) ++r->count;
                }
            }
            nvs_close(h);
        }
        heap_caps_free(tmp);
    }
    for (auto& e : r->e) { e.title[sizeof(e.title) - 1] = 0; e.msg[sizeof(e.msg) - 1] = 0; }
}

void save(int k) {
    // copia sotto lock, scrittura fuori
    Ring* copy = static_cast<Ring*>(heap_caps_malloc(sizeof(Ring), MALLOC_CAP_SPIRAM));
    if (!copy) return;
    { std::lock_guard<std::mutex> l(g_mx); std::memcpy(copy, g_ring[k], sizeof(Ring)); }
    bool done = false;
    if (sd_ok()) {
        if (FILE* f = std::fopen(file_of(k), "wb")) {
            done = std::fwrite(copy, 1, sizeof(Ring), f) == sizeof(Ring);
            std::fclose(f);
        }
    }
    if (!done) {   // senza microSD: le ultime voci in NVS
        Entry* tmp = static_cast<Entry*>(heap_caps_malloc(kNvsKeep * sizeof(Entry), MALLOC_CAP_SPIRAM));
        if (!tmp) { heap_caps_free(copy); return; }
        const int n = copy->count < kNvsKeep ? copy->count : kNvsKeep;
        for (int i = 0; i < n; ++i) tmp[i] = copy->e[(copy->head + kMax - n + i) % kMax];
        nvs_handle_t h;
        if (nvs_open("abarth_ntf", NVS_READWRITE, &h) == ESP_OK) {
            nvs_set_blob(h, nvs_key(k), tmp, n * sizeof(Entry));
            nvs_commit(h);
            nvs_close(h);
        }
        heap_caps_free(tmp);
    }
    heap_caps_free(copy);
}

void io_task(void*) {
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        vTaskDelay(pdMS_TO_TICKS(1500));   // raggruppa piu' avvisi ravvicinati
        const uint8_t d = g_dirty.exchange(0);
        for (int k = 0; k < 2; ++k) if (d & (1 << k)) save(k);
    }
}

void mark_dirty(int k) {
    g_dirty.fetch_or(1 << k);
    g_rev.fetch_add(1);
    if (g_io) xTaskNotifyGive(g_io);
}

int cur() { return settings::current().demo_mode ? 1 : 0; }
}  // namespace

void init() {
    std::lock_guard<std::mutex> l(g_mx);
    if (g_ring[0]) return;
    for (int k = 0; k < 2; ++k) {
        void* mem = heap_caps_malloc(sizeof(Ring), MALLOC_CAP_SPIRAM);
        if (!mem) return;
        g_ring[k] = static_cast<Ring*>(mem);   // niente temporanei sulla pila (32 KB)
        reset_ring(g_ring[k]);
        load(k);
    }
    xTaskCreate(io_task, "notifiche", 4096, nullptr, 1, &g_io);
    ESP_LOGI(TAG, "cronologia avvisi: %u reali, %u simulati (%s)", g_ring[0]->count, g_ring[1]->count,
             sd_ok() ? "microSD" : "NVS");
}

void add(uint8_t level, const char* title, const char* msg) {
    const int k = cur();
    {
        std::lock_guard<std::mutex> l(g_mx);
        Ring* r = g_ring[k];
        if (!r) return;
        Entry& e = r->e[r->head];
        std::memset(static_cast<void*>(&e), 0, sizeof(e));
        const time_t now = time(nullptr);
        struct tm lt;
        localtime_r(&now, &lt);
        e.epoch = lt.tm_year + 1900 >= 2024 ? static_cast<uint32_t>(now) : 0;
        e.level = level > 2 ? 2 : level;
        std::snprintf(e.title, sizeof(e.title), "%s", title ? title : "");
        std::snprintf(e.msg, sizeof(e.msg), "%s", msg ? msg : "");
        r->head = (r->head + 1) % kMax;
        if (r->count < kMax) ++r->count;
    }
    mark_dirty(k);
}

std::vector<Entry> list() {
    std::vector<Entry> v;
    std::lock_guard<std::mutex> l(g_mx);
    Ring* r = g_ring[cur()];
    if (!r) return v;
    v.reserve(r->count);
    for (int i = 1; i <= r->count; ++i) v.push_back(r->e[(r->head + kMax - i) % kMax]);
    return v;
}

int unread() {
    std::lock_guard<std::mutex> l(g_mx);
    Ring* r = g_ring[cur()];
    if (!r) return 0;
    int n = 0;
    for (int i = 1; i <= r->count; ++i) n += r->e[(r->head + kMax - i) % kMax].read ? 0 : 1;
    return n;
}

void markAllRead() {
    const int k = cur();
    {
        std::lock_guard<std::mutex> l(g_mx);
        Ring* r = g_ring[k];
        if (!r) return;
        for (auto& e : r->e) e.read = 1;
    }
    mark_dirty(k);
}

void clear() {
    const int k = cur();
    {
        std::lock_guard<std::mutex> l(g_mx);
        if (!g_ring[k]) return;
        reset_ring(g_ring[k]);
    }
    mark_dirty(k);
}

bool showingSimulated() { return cur() == 1; }
uint32_t revision() { return g_rev.load(); }

}  // namespace abarth::notify
