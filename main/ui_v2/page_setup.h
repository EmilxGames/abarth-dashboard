#pragma once
#include <cstdint>
struct _lv_obj_t;
typedef struct _lv_obj_t lv_obj_t;
/// Pagina IMPOSTAZIONI della UI Moderno: righe su schede raggruppate.
namespace abarth::ui_v2::setup_page {
void build(lv_obj_t* parent, int32_t w, int32_t h);
void update(int64_t now_ms);
/// Lista scorrevole (per il benchmark)
lv_obj_t* scrollTarget();
}
