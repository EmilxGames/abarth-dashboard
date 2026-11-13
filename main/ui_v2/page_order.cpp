#include "ui_v2/page_order.h"

#include <cstring>
#ifdef ESP_PLATFORM
#include <esp_log.h>
#include <nvs.h>
#endif

namespace abarth::ui_v2::page_order {

Order defaults() { return Order{}; }

bool validate(Order& o) {
    bool ok = o.version == kVersion && o.reserved == 0;
    bool seen[kData] = {};
    for (int i = 0; i < kData && ok; ++i) {
        if (o.order[i] >= kData || seen[o.order[i]]) ok = false;
        else seen[o.order[i]] = true;
    }
    o.hidden &= (1u << kData) - 1;
    if (!ok) o = defaults();
    if ((o.hidden & ((1u << kData) - 1)) == (1u << kData) - 1) {   // almeno una visibile
        o.hidden = 0;
        ok = false;
    }
    return ok;
}

bool isHidden(const Order& o, int page) { return page >= 0 && page < kData && (o.hidden >> page) & 1; }

int visible(const Order& o, uint8_t* out) {
    int n = 0;
    for (int i = 0; i < kData; ++i)
        if (!isHidden(o, o.order[i])) out[n++] = o.order[i];
    return n;
}

bool moveUp(Order& o, int pos) {
    if (pos <= 0 || pos >= kData) return false;
    const uint8_t t = o.order[pos - 1];
    o.order[pos - 1] = o.order[pos];
    o.order[pos] = t;
    return true;
}

bool moveDown(Order& o, int pos) { return pos >= 0 && pos < kData - 1 && moveUp(o, pos + 1); }

bool setVisible(Order& o, int page, bool on) {
    if (page < 0 || page >= kData) return false;
    if (on) {
        o.hidden &= ~(1u << page);
        return true;
    }
    uint8_t tmp[kData];
    if (!isHidden(o, page) && visible(o, tmp) <= 1) return false;
    o.hidden |= 1u << page;
    return true;
}

#ifdef ESP_PLATFORM
namespace {
constexpr const char* kNs = "abarth_v2";
constexpr const char* kKey = "page_order";
constexpr const char* TAG = "page_order";
}

Order load() {
    Order o;
    size_t sz = sizeof(o);
    nvs_handle_t h;
    if (nvs_open(kNs, NVS_READONLY, &h) == ESP_OK) {
        const esp_err_t err = nvs_get_blob(h, kKey, &o, &sz);
        nvs_close(h);
        if (err == ESP_OK && sz == sizeof(o)) {
            if (!validate(o)) ESP_LOGW(TAG, "ordine pagine non valido: uso quello predefinito");
            return o;
        }
    }
    return defaults();
}

bool save(const Order& in) {
    Order o = in;
    validate(o);
    nvs_handle_t h;
    if (nvs_open(kNs, NVS_READWRITE, &h) != ESP_OK) return false;
    esp_err_t err = nvs_set_blob(h, kKey, &o, sizeof(o));
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err != ESP_OK) ESP_LOGE(TAG, "salvataggio ordine pagine fallito: %s", esp_err_to_name(err));
    return err == ESP_OK;
}
#endif

}  // namespace abarth::ui_v2::page_order
