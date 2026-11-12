#pragma once
/**
 * @file async_ui.h
 * @brief Esecuzione thread-safe per la UI v2 (stesso schema della Classica):
 *  - postToUi: esegue nel task LVGL (lv_async_call sotto lock);
 *  - runInWorker: lavoro lento (DTC, SD, WiFi) fuori dal task LVGL.
 */
#include <functional>

namespace abarth::ui_v2::async {
void postToUi(std::function<void()> fn);
/// false se c'e' gia' un'operazione in corso (doppio tocco)
bool runInWorker(std::function<void()> work);
}
