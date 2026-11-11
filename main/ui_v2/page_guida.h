#pragma once
#include <cstdint>
struct _lv_obj_t;
typedef struct _lv_obj_t lv_obj_t;
namespace abarth::ui_v2::guida {
void build(lv_obj_t* parent, int32_t w, int32_t h);
void update(int64_t now_ms);
}
