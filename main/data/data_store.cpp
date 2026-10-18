#include "data/data_store.h"

#include "esp_timer.h"

namespace abarth::data {

namespace {
DataStore g_store;

int64_t now_ms() { return esp_timer_get_time() / 1000; }
}  // namespace

DataStore& store() { return g_store; }

DataStore::DataStore() = default;

DataStore::~DataStore() {
    if (mutex_) {
        vSemaphoreDelete(mutex_);
        mutex_ = nullptr;
    }
}

bool DataStore::init() {
    if (mutex_) return true;
    mutex_ = xSemaphoreCreateMutex();
    return mutex_ != nullptr;
}

void DataStore::set(Metric m, float value) {
    const auto idx = static_cast<size_t>(m);
    if (idx >= static_cast<size_t>(Metric::Count_)) return;
    if (!mutex_) return;
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(10)) == pdTRUE) {
        samples_[idx].value      = value;
        samples_[idx].updated_ms = now_ms();
        samples_[idx].supported  = true;
        xSemaphoreGive(mutex_);
    }
}

Sample DataStore::get(Metric m) const {
    const auto idx = static_cast<size_t>(m);
    Sample out{};
    if (idx >= static_cast<size_t>(Metric::Count_)) return out;
    if (!mutex_) return out;
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(10)) == pdTRUE) {
        out = samples_[idx];
        xSemaphoreGive(mutex_);
    }
    return out;
}

void DataStore::invalidate(Metric m) {
    const auto idx = static_cast<size_t>(m);
    if (idx >= static_cast<size_t>(Metric::Count_)) return;
    if (!mutex_) return;
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(10)) == pdTRUE) {
        samples_[idx] = Sample{};
        xSemaphoreGive(mutex_);
    }
}

void DataStore::markNotSupported(Metric m) {
    const auto idx = static_cast<size_t>(m);
    if (idx >= static_cast<size_t>(Metric::Count_)) return;
    if (!mutex_) return;
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(10)) == pdTRUE) {
        samples_[idx].supported  = false;
        samples_[idx].updated_ms = now_ms();
        xSemaphoreGive(mutex_);
    }
}

void DataStore::clearSupportFlags() {
    if (!mutex_) return;
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(10)) == pdTRUE) {
        for (auto& s : samples_) {
            s.supported = true;
        }
        xSemaphoreGive(mutex_);
    }
}

}  // namespace abarth::data
