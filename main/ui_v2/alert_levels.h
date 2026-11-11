#pragma once
/**
 * @file alert_levels.h
 * @brief Livello (ok / avviso / critico) di una grandezza in base alle soglie
 *        configurate nell'alert_manager, per i pallini di stato delle schede.
 */
#include "data/data_store.h"
#include "ui_v2/binding.h"

namespace abarth::ui_v2 {
/// true se la grandezza ha soglie d'allarme (mostra il pallino)
bool hasAlertThreshold(data::Metric m);
/// Livello: Normal = ok (verde), Low = avviso (ambra), Warn = critico (rosso)
Level alertLevel(data::Metric m, float value);
}
