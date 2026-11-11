#pragma once
/**
 * @file page_grid.h
 * @brief Pagina dati generica: una scheda larga (opzionale) + schede piccole
 *        in 2 colonne. Usata per Temperature, Pressioni, Elettrico/Carburante.
 */
#include <cstdint>
#include "data/data_store.h"

struct _lv_obj_t;
typedef struct _lv_obj_t lv_obj_t;

namespace abarth::ui_v2::grid {

constexpr int kMaxCards = 8;

struct PageDef {
    bool          wide_first;            ///< la prima grandezza va in una scheda larga
    data::Metric  metrics[kMaxCards];
    int           count;
};

struct GridPage;   // stato interno
GridPage* create(lv_obj_t* parent, int32_t w, int32_t y0, const PageDef& def);
void update(GridPage* p, int64_t now_ms);

}  // namespace abarth::ui_v2::grid
