#include "ui_v2/async_ui.h"

#include <atomic>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "display/lvgl_v8_port.h"

namespace abarth::ui_v2::async {
namespace {
QueueHandle_t g_q = nullptr;
std::atomic<bool> g_busy{false};

void worker(void*) {
    std::function<void()>* job = nullptr;
    while (true) {
        if (xQueueReceive(g_q, &job, portMAX_DELAY) == pdTRUE && job) {
            (*job)();
            delete job;
            g_busy.store(false);
        }
    }
}
}  // namespace

void postToUi(std::function<void()> fn) {
    auto* f = new std::function<void()>(std::move(fn));
    if (lvgl_port_lock(-1)) {
        lv_async_call([](void* p) {
            auto* fp = static_cast<std::function<void()>*>(p);
            (*fp)();
            delete fp;
        }, f);
        lvgl_port_unlock();
    } else {
        delete f;
    }
}

bool runInWorker(std::function<void()> work) {
    if (!g_q) {
        g_q = xQueueCreate(4, sizeof(std::function<void()>*));
        if (!g_q) return false;
        xTaskCreatePinnedToCore(worker, "v2_worker", 6144, nullptr, 2, nullptr, 0);
    }
    bool expected = false;
    if (!g_busy.compare_exchange_strong(expected, true)) return false;
    auto* job = new std::function<void()>(std::move(work));
    if (xQueueSend(g_q, &job, 0) != pdTRUE) {
        delete job;
        g_busy.store(false);
        return false;
    }
    return true;
}
}  // namespace abarth::ui_v2::async
