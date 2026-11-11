#pragma once
#include <cstdint>
#include "ui_v2/ui_v2.h"

struct _lv_obj_t;
typedef struct _lv_obj_t lv_obj_t;

/// Pagine della UI v2: ogni pagina costruisce i suoi oggetti una volta sola
/// e li aggiorna nel timer solo se e' visibile.
namespace abarth::ui_v2::pages {
void build(Page p, lv_obj_t* parent, int32_t w, int32_t h, int32_t y0);
void update(Page p, int64_t now_ms);
void onShow(Page p);
}  // namespace abarth::ui_v2::pages
