#pragma once
/**
 * @file alert_banner.h
 * @brief Banner degli allarmi nello stile Moderno (scheda in alto, non modale).
 *        Usato da alert_manager quando e' attiva la UI v2.
 */
#include <cstdint>
#include "lvgl.h"

namespace abarth::ui_v2 {
enum class AlertLevel : uint8_t { Info, Warning, Critical };
/// true se la UI Moderno e' attiva (banner nuovo invece del pop-up classico)
bool alertBannerActive();
/// Costruisce il banner dentro `host`; *out_timer riceve la label del conto alla rovescia
void buildAlertBanner(lv_obj_t* host, const char* title, const char* msg, AlertLevel level,
                      const char* ok_label, bool countdown, lv_obj_t** out_timer, lv_event_cb_t on_ok);
}
