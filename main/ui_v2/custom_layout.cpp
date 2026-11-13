#include "ui_v2/custom_layout.h"

#include <cstring>
#include "data/data_store.h"

#ifdef ESP_PLATFORM
#include <esp_log.h>
#include <nvs.h>
#endif

namespace abarth::ui_v2::custom {

namespace {
constexpr uint8_t kMetricCount = static_cast<uint8_t>(data::Metric::Count_);
bool metric_ok(uint8_t m) { return m < kMetricCount; }
bool idx_ok(const Layout& l, int i) { return i >= 0 && i < l.count; }
}  // namespace

bool size_ok(uint8_t c, uint8_t r) { return (c == 1 || c == 2 || c == 4) && (r == 1 || r == 2); }

bool typeAllowed(uint8_t cols, uint8_t rows, WidgetType t) {
    (void)rows;
    if (t == WidgetType::BigNumber) return true;
    return cols >= 2;   // barra e min/max non entrano in 1/4 di larghezza
}

int pack(const Layout& l, Placement out[kMaxCards]) {
    bool occ[kMaxRows][kCols] = {};
    int used = 0;
    for (int i = 0; i < l.count && i < kMaxCards; ++i) {
        const int cw = l.cards[i].cols, ch = l.cards[i].rows;
        bool placed = false;
        for (int r = 0; r + ch <= kMaxRows && !placed; ++r) {
            for (int c = 0; c + cw <= kCols && !placed; ++c) {
                bool free = true;
                for (int rr = r; rr < r + ch && free; ++rr)
                    for (int cc = c; cc < c + cw && free; ++cc) free = !occ[rr][cc];
                if (!free) continue;
                for (int rr = r; rr < r + ch; ++rr)
                    for (int cc = c; cc < c + cw; ++cc) occ[rr][cc] = true;
                out[i] = {static_cast<uint8_t>(c), static_cast<uint8_t>(r), static_cast<uint8_t>(cw), static_cast<uint8_t>(ch)};
                if (r + ch > used) used = r + ch;
                placed = true;
            }
        }
        if (!placed) out[i] = {0, 0, 0, 0};
    }
    return used;
}

Layout defaultLayout() {
    using data::Metric;
    Layout l;
    add(l, static_cast<uint8_t>(Metric::Rpm), WidgetType::NumberBar, 4, 2);
    add(l, static_cast<uint8_t>(Metric::Speed), WidgetType::BigNumber, 2, 2);
    add(l, static_cast<uint8_t>(Metric::BoostRelBar), WidgetType::PeakMinMax, 2, 2);
    add(l, static_cast<uint8_t>(Metric::CoolantTempC), WidgetType::BigNumber, 2, 1);
    add(l, static_cast<uint8_t>(Metric::OilTempC), WidgetType::BigNumber, 2, 1);
    add(l, static_cast<uint8_t>(Metric::IntakeAirTempC), WidgetType::BigNumber, 1, 1);
    add(l, static_cast<uint8_t>(Metric::AmbientTempC), WidgetType::BigNumber, 1, 1);
    add(l, static_cast<uint8_t>(Metric::BatteryVolts), WidgetType::BigNumber, 1, 1);
    add(l, static_cast<uint8_t>(Metric::Throttle), WidgetType::BigNumber, 1, 1);
    return l;
}

bool validate(Layout& l, bool fix) {
    bool ok = l.version == kLayoutVersion && l.count <= kMaxCards;
    if (!ok) {
        if (fix) l = defaultLayout();
        return false;
    }
    // compatta togliendo le schede non valide
    int w = 0;
    for (int i = 0; i < l.count; ++i) {
        Card c = l.cards[i];
        bool good = metric_ok(c.metric);
        if (c.type >= static_cast<uint8_t>(WidgetType::Count)) { ok = false; c.type = 0; }
        if (!size_ok(c.cols, c.rows)) { ok = false; c.cols = 2; c.rows = 2; }
        if (!typeAllowed(c.cols, c.rows, static_cast<WidgetType>(c.type))) { ok = false; c.type = 0; }
        if (!good) { ok = false; continue; }
        if (fix) l.cards[w] = c;
        ++w;
    }
    if (fix) l.count = static_cast<uint8_t>(w);
    return ok;
}

int add(Layout& l, uint8_t metric, WidgetType t, uint8_t cols, uint8_t rows) {
    if (l.count >= kMaxCards || !metric_ok(metric) || !size_ok(cols, rows)) return -1;
    if (!typeAllowed(cols, rows, t)) t = WidgetType::BigNumber;
    l.cards[l.count] = Card{metric, static_cast<uint8_t>(t), cols, rows};
    return l.count++;
}

bool remove(Layout& l, int i) {
    if (!idx_ok(l, i)) return false;
    for (int k = i; k + 1 < l.count; ++k) l.cards[k] = l.cards[k + 1];
    --l.count;
    return true;
}

bool setMetric(Layout& l, int i, uint8_t metric) {
    if (!idx_ok(l, i) || !metric_ok(metric)) return false;
    l.cards[i].metric = metric;
    return true;
}

bool setType(Layout& l, int i, WidgetType t) {
    if (!idx_ok(l, i) || t >= WidgetType::Count) return false;
    if (!typeAllowed(l.cards[i].cols, l.cards[i].rows, t)) return false;
    l.cards[i].type = static_cast<uint8_t>(t);
    return true;
}

bool setSize(Layout& l, int i, uint8_t cols, uint8_t rows) {
    if (!idx_ok(l, i) || !size_ok(cols, rows)) return false;
    l.cards[i].cols = cols;
    l.cards[i].rows = rows;
    if (!typeAllowed(cols, rows, static_cast<WidgetType>(l.cards[i].type))) l.cards[i].type = 0;
    return true;
}

bool swap(Layout& l, int a, int b) {
    if (!idx_ok(l, a) || !idx_ok(l, b) || a == b) return false;
    const Card t = l.cards[a];
    l.cards[a] = l.cards[b];
    l.cards[b] = t;
    return true;
}

bool migrateV1(const uint8_t* blob, int len, Layout& out) {
    // v1: version(1) reserved[3] (reserved[0]=suggerimento visto) + 8 slot da 4 byte
    if (!blob || len != 36 || blob[0] != 1) return false;
    out = Layout{};
    out.flags = blob[1] ? 1 : 0;
    for (int s = 0; s < 8; ++s) {
        const uint8_t* p = blob + 4 + s * 4;
        const uint8_t metric = p[0], type = p[1], span = p[2];
        const bool covered = (s % 2 == 1) && blob[4 + (s - 1) * 4 + 2] == 2;
        if (covered || metric == kEmpty || !metric_ok(metric)) continue;
        add(out, metric, type < static_cast<uint8_t>(WidgetType::Count) ? static_cast<WidgetType>(type) : WidgetType::BigNumber,
            span == 2 ? 4 : 2, 2);
    }
    return true;
}

#ifdef ESP_PLATFORM
namespace {
constexpr const char* kNs = "abarth_v2";
constexpr const char* kKey = "custom_lay";
constexpr const char* TAG = "custom_layout";
}

Layout load() {
    uint8_t buf[sizeof(Layout)];
    size_t sz = sizeof(buf);
    nvs_handle_t h;
    if (nvs_open(kNs, NVS_READONLY, &h) == ESP_OK) {
        const esp_err_t err = nvs_get_blob(h, kKey, buf, &sz);
        nvs_close(h);
        if (err == ESP_OK) {
            Layout l;
            if (sz == sizeof(Layout) && buf[0] == kLayoutVersion) {
                std::memcpy(&l, buf, sizeof(l));
                if (!validate(l, true)) ESP_LOGW(TAG, "layout PERSONALE corretto (dati non coerenti)");
                return l;
            }
            if (migrateV1(buf, static_cast<int>(sz), l)) {
                ESP_LOGI(TAG, "layout PERSONALE convertito dalla versione 1 (%u schede)", l.count);
                return l;
            }
        }
    }
    return defaultLayout();
}

bool save(const Layout& in) {
    Layout l = in;
    validate(l, true);
    nvs_handle_t h;
    if (nvs_open(kNs, NVS_READWRITE, &h) != ESP_OK) return false;
    esp_err_t err = nvs_set_blob(h, kKey, &l, sizeof(l));
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err != ESP_OK) ESP_LOGE(TAG, "salvataggio layout fallito: %s", esp_err_to_name(err));
    return err == ESP_OK;
}
#endif

}  // namespace abarth::ui_v2::custom
