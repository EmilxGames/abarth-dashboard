// Test host della logica ordine/visibilita' pagine (ui_v2/page_order)
#include <cassert>
#include <cstdio>
#include "ui_v2/page_order.h"
using namespace abarth::ui_v2::page_order;

int main() {
    Order o = defaults();
    assert(validate(o));
    uint8_t v[kData];
    assert(visible(o, v) == 5 && v[0] == 0 && v[4] == 4);
    // PERSONALE (4) in cima: quattro spostamenti in su
    for (int p = 4; p > 0; --p) assert(moveUp(o, p));
    assert(!moveUp(o, 0));
    assert(o.order[0] == 4 && o.order[1] == 0 && o.order[4] == 3);
    assert(moveDown(o, 0) && o.order[1] == 4 && o.order[0] == 0);
    assert(!moveDown(o, kData - 1));
    // nascondi tutte tranne una: l'ultima non si puo' nascondere
    assert(setVisible(o, 1, false) && setVisible(o, 2, false) && setVisible(o, 3, false) && setVisible(o, 4, false));
    assert(!setVisible(o, 0, false));
    assert(visible(o, v) == 1 && v[0] == 0);
    assert(setVisible(o, 3, true) && visible(o, v) == 2);
    // dati corrotti -> predefiniti
    Order bad = defaults();
    bad.order[2] = 1;
    assert(!validate(bad) && bad.order[2] == 2);
    Order all_hidden = defaults();
    all_hidden.hidden = 0x1F;
    assert(!validate(all_hidden) && all_hidden.hidden == 0);
    Order old = defaults();
    old.version = 9;
    assert(!validate(old) && old.version == kVersion);
    std::puts("OK test ordine pagine");
    return 0;
}
