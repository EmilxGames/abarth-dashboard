#pragma once
#include <cstdint>
struct _lv_obj_t;
typedef struct _lv_obj_t lv_obj_t;
namespace abarth::ui_v2::guida {
/// y0 = coordinata y della pagina sullo schermo (il design usa coordinate schermo)
void build(lv_obj_t* parent, int32_t w, int32_t h, int32_t y0);
void update(int64_t now_ms);
}
