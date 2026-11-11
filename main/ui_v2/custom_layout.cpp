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

bool metric_ok(uint8_t m) { return m == kEmpty || m < kMetricCount; }
int col(int slot) { return slot % kCols; }
bool slot_ok(int s) { return s >= 0 && s < kSlots; }
}  // namespace

Layout defaultLayout() {
    using data::Metric;
    Layout l;
    auto put = [&](int s, Metric m, WidgetType t, uint8_t span = 1) {
        l.slots[s].metric = static_cast<uint8_t>(m);
        l.slots[s].type = static_cast<uint8_t>(t);
        l.slots[s].span = span;
    };
    put(0, Metric::Rpm, WidgetType::NumberBar, 2);           // riga 0: giri a tutta larghezza
    put(2, Metric::Speed, WidgetType::BigNumber);
    put(3, Metric::BoostRelBar, WidgetType::PeakMinMax);
    put(4, Metric::CoolantTempC, WidgetType::BigNumber);
    put(5, Metric::OilTempC, WidgetType::BigNumber);
    put(6, Metric::IntakeAirTempC, WidgetType::BigNumber);
    put(7, Metric::BatteryVolts, WidgetType::BigNumber);
    return l;
}

bool isCovered(const Layout& l, int slot) {
    if (!slot_ok(slot) || col(slot) == 0) return false;
    return l.slots[slot - 1].span == 2;
}

bool validate(Layout& l, bool fix) {
    bool ok = l.version == kLayoutVersion;
    if (!ok && fix) {
        l = defaultLayout();
        return false;
    }
    for (int s = 0; s < kSlots; ++s) {
        Slot& sl = l.slots[s];
        if (!metric_ok(sl.metric)) { ok = false; if (fix) sl.metric = kEmpty; }
        if (sl.type >= static_cast<uint8_t>(WidgetType::Count)) { ok = false; if (fix) sl.type = 0; }
        if (sl.span != 1 && sl.span != 2) { ok = false; if (fix) sl.span = 1; }
        if (sl.span == 2 && col(s) != 0) { ok = false; if (fix) sl.span = 1; }
    }
    // lo slot coperto da uno largo deve essere vuoto e stretto
    for (int s = 0; s < kSlots; ++s) {
        if (isCovered(l, s) && (l.slots[s].metric != kEmpty || l.slots[s].span != 1)) {
            ok = false;
            if (fix) l.slots[s] = Slot{};
        }
    }
    return ok;
}

bool setMetric(Layout& l, int slot, uint8_t metric) {
    if (!slot_ok(slot) || !metric_ok(metric) || isCovered(l, slot)) return false;
    l.slots[slot].metric = metric;
    return true;
}

bool setType(Layout& l, int slot, WidgetType t) {
    if (!slot_ok(slot) || t >= WidgetType::Count || isCovered(l, slot)) return false;
    l.slots[slot].type = static_cast<uint8_t>(t);
    return true;
}

bool setSpan(Layout& l, int slot, uint8_t span) {
    if (!slot_ok(slot) || (span != 1 && span != 2) || isCovered(l, slot)) return false;
    if (span == 2) {
        if (col(slot) != 0) return false;
        l.slots[slot + 1] = Slot{};  // lo slot a destra viene coperto
    }
    l.slots[slot].span = span;
    return true;
}

bool swap(Layout& l, int a, int b) {
    if (!slot_ok(a) || !slot_ok(b) || a == b) return false;
    if (isCovered(l, a) || isCovered(l, b)) return false;
    const Slot sa = l.slots[a], sb = l.slots[b];
    // uno slot largo puo' andare solo in colonna 0, e la destinazione deve poter
    // coprire lo slot a destra: si scambiano le righe intere.
    if (sa.span == 2 || sb.span == 2) {
        if (col(a) != 0 || col(b) != 0) return false;
        const Slot ra = l.slots[a + 1], rb = l.slots[b + 1];
        l.slots[a] = sb; l.slots[a + 1] = rb;
        l.slots[b] = sa; l.slots[b + 1] = ra;
        return true;
    }
    l.slots[a] = sb;
    l.slots[b] = sa;
    return true;
}

#ifdef ESP_PLATFORM
namespace {
constexpr const char* kNs = "abarth_v2";
constexpr const char* kKey = "custom_lay";
constexpr const char* TAG = "custom_layout";
}

Layout load() {
    Layout l;
    nvs_handle_t h;
    size_t sz = sizeof(l);
    if (nvs_open(kNs, NVS_READONLY, &h) == ESP_OK) {
        const esp_err_t err = nvs_get_blob(h, kKey, &l, &sz);
        nvs_close(h);
        if (err == ESP_OK && sz == sizeof(l)) {
            if (!validate(l, true)) ESP_LOGW(TAG, "layout PERSONALE corretto (dati non coerenti)");
            return l;
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
