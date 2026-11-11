#pragma once
/**
 * @file metric_info.h
 * @brief Catalogo delle grandezze mostrabili (nome, unita', scala, soglie).
 *        Indipendente dallo stile grafico.
 */
#include <cstdint>
#include "data/data_store.h"
#include "ui_v2/data_fmt.h"

namespace abarth::ui_v2 {

struct MetricInfo {
    data::Metric metric;
    const char*  name;       ///< nome breve (titolo riquadro)
    const char*  long_name;  ///< nome completo (lista di scelta)
    fmt::Kind    kind;       ///< conversione unita'
    const char*  unit;       ///< unita' se kind == Plain
    uint8_t      decimals;
    float        min_v, max_v;          ///< scala barra
    float        warn_low, warn_high;   ///< soglie colore (NaN = nessuna)
};

/// Tutte le grandezze del data store, nell'ordine della lista di scelta.
const MetricInfo* metricCatalog(size_t& count);

/// Info di una grandezza (nullptr se sconosciuta).
const MetricInfo* metricInfo(data::Metric m);

}  // namespace abarth::ui_v2
