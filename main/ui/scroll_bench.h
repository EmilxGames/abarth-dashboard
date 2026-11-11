#pragma once
struct _lv_obj_t;
typedef struct _lv_obj_t lv_obj_t;

namespace abarth::ui::scroll_bench {
/// Avvia il benchmark (solo se CONFIG_ABARTH_SCROLL_BENCH=y, altrimenti no-op).
void start(lv_obj_t* tabview, uint32_t settings_tab_idx, lv_obj_t* scroll_target);
}
