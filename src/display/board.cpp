#include "display/board.h"

#include <Arduino.h>
#include <esp_display_panel.hpp>
#include <esp_log.h>
#include <lvgl.h>

#include "display/lvgl_v8_port.h"

namespace abarth::display {

namespace {
constexpr const char* TAG = "display";

using esp_panel::board::Board;
Board* g_board = nullptr;
}  // namespace

bool init() {
    ESP_LOGI(TAG, "inizializzazione board...");
    g_board = new Board();
    if (!g_board) {
        ESP_LOGE(TAG, "alloc Board fallita");
        return false;
    }
    g_board->init();
    if (!g_board->begin()) {
        ESP_LOGE(TAG, "board begin() fallito");
        return false;
    }

    ESP_LOGI(TAG, "inizializzazione LVGL port...");
    if (!lvgl_port_init(g_board->getLCD(), g_board->getTouch())) {
        ESP_LOGE(TAG, "lvgl_port_init fallito");
        return false;
    }
    setBrightness(80);
    ESP_LOGI(TAG, "display pronto");
    return true;
}

void setBrightness(int percent) {
    if (!g_board) return;
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    if (auto* bl = g_board->getBacklight()) {
        bl->setBrightness(percent);
    }
}

void tick() {
    // La lvgl port del v8 crea un suo task di tick/refresh: qui nulla.
}

}  // namespace abarth::display
