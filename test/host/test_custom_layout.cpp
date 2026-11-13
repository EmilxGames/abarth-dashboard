#include <cassert>
#include <cstdio>
#include "ui_v2/custom_layout.h"  // da main/
using namespace abarth::ui_v2::custom;

int main() {
    Layout l = defaultLayout();
    assert(validate(l, false));
    Placement p[kMaxCards];
    int rows = pack(l, p);
    // giri intera alta, poi due 1/2 alte, poi due 1/2 basse sulla stessa riga, quattro 1/4 su una riga
    assert(p[0].col == 0 && p[0].row == 0 && p[0].cols == 4 && p[0].rows == 2);
    assert(p[1].col == 0 && p[1].row == 2 && p[2].col == 2 && p[2].row == 2);
    assert(p[3].row == 4 && p[3].col == 0 && p[4].row == 4 && p[4].col == 2);
    assert(p[5].row == 5 && p[8].row == 5 && p[8].col == 3);
    assert(rows == 6);
    // una 1/4 dopo una 1/2 alta riempie il buco accanto
    Layout g; add(g, 0, WidgetType::BigNumber, 2, 2); add(g, 1, WidgetType::BigNumber, 1, 1);
    pack(g, p);
    assert(p[1].col == 2 && p[1].row == 0);
    // barra e min/max non ammessi in 1/4
    assert(!setType(g, 1, WidgetType::NumberBar) && setType(g, 0, WidgetType::NumberBar));
    assert(setSize(g, 0, 1, 2) && g.cards[0].type == 0);   // ridotta a 1/4: torna numero
    // modifica
    assert(setSize(l, 1, 1, 1) && swap(l, 0, 1) && remove(l, 2));
    assert(l.count == 8 && l.cards[0].cols == 1);
    for (int i = 0; i < 40; ++i) add(l, 0, WidgetType::BigNumber, 1, 1);
    assert(l.count == kMaxCards && add(l, 0, WidgetType::BigNumber, 1, 1) == -1);
    Layout bad = defaultLayout(); bad.cards[2].metric = 200; bad.cards[3].cols = 3;
    assert(!validate(bad, true) && bad.count == 8 && bad.cards[2].cols == 2);
    // migrazione v1 (blob reale dal display di Emil)
    const uint8_t v1[36] = {1,0,0,0, 0,1,2,0, 0xff,0,1,0, 1,0,1,0, 10,2,1,0, 4,0,1,0, 7,0,1,0, 5,0,1,0, 0,0,1,0};
    Layout m;
    assert(migrateV1(v1, 36, m));
    assert(m.count == 7 && m.cards[0].metric == 0 && m.cards[0].cols == 4 && m.cards[0].rows == 2);
    assert(m.cards[1].metric == 1 && m.cards[1].cols == 2 && m.cards[2].metric == 10 && m.cards[2].type == 2);
    printf("OK test layout PERSONALE v2\n");
}
