#include "ui_v2/page_grid.h"

#include <new>
#include "lvgl.h"
#include "ui_v2/alert_levels.h"
#include "ui_v2/binding.h"
#include "ui_v2/metric_info.h"
#include "ui_v2/theme.h"
#include "ui_v2/widgets.h"

namespace abarth::ui_v2::grid {

using namespace theme;

struct GridPage {
    int            count = 0;
    bool           wide_first = false;
    w::WideCard    wide;
    w::ValueCard   cards[kMaxCards];
    Binding        bind[kMaxCards];
    data::Metric   metric[kMaxCards];
};

GridPage* create(lv_obj_t* parent, int32_t W, int32_t y0, const PageDef& def) {
    auto* g = new (std::nothrow) GridPage();
    if (!g) return nullptr;
    g->count = def.count > kMaxCards ? kMaxCards : def.count;
    g->wide_first = def.wide_first;
    const int32_t cw = W - 2 * kMargin;
    const int32_t half = (cw - kGap) / 2;
    const int32_t rows = 4;
    const int32_t row_h = (kCardsBot - kCardsTop - (rows - 1) * kGap) / rows;
    int32_t y = kCardsTop - y0;
    int idx = 0;
    if (g->wide_first && g->count > 0) {
        const MetricInfo* mi = metricInfo(def.metrics[0]);
        g->wide.create(parent, kMargin, y, cw, row_h, mi ? mi->name : "", hasAlertThreshold(def.metrics[0]));
        g->metric[0] = def.metrics[0];
        g->bind[0].bind(def.metrics[0]);
        y += row_h + kGap;
        idx = 1;
    }
    for (int col = 0; idx < g->count; ++idx) {
        const MetricInfo* mi = metricInfo(def.metrics[idx]);
        const int32_t x = kMargin + (col ? half + kGap : 0);
        const int32_t wdt = col ? cw - half - kGap : half;
        g->cards[idx].create(parent, x, y, wdt, row_h, mi ? mi->name : "", hasAlertThreshold(def.metrics[idx]));
        g->metric[idx] = def.metrics[idx];
        g->bind[idx].bind(def.metrics[idx]);
        if (col) y += row_h + kGap;
        col ^= 1;
    }
    return g;
}

void update(GridPage* g, int64_t now_ms) {
    if (!g) return;
    for (int i = 0; i < g->count; ++i) {
        Binding& b = g->bind[i];
        b.update(now_ms);
        const Level dl = b.level == Level::Missing ? Level::Missing : alertLevel(g->metric[i], b.value);
        if (i == 0 && g->wide_first) g->wide.set(b, dl);
        else g->cards[i].set(b, dl);
    }
}

}  // namespace abarth::ui_v2::grid
