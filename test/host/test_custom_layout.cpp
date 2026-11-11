#include <cassert>
#include <cstdio>
#include "ui_v2/custom_layout.h"  // da main/
using namespace abarth::ui_v2::custom;
int main() {
  Layout l = defaultLayout();
  assert(validate(l, false));
  assert(isCovered(l, 1));
  assert(!setMetric(l, 1, 3));            // coperto
  assert(setSpan(l, 0, 1));               // riga 0 torna a 2 colonne
  assert(setMetric(l, 1, 3));
  assert(!setSpan(l, 1, 2));              // span 2 solo colonna 0
  assert(setSpan(l, 4, 2) && l.slots[5].metric == kEmpty);
  assert(swap(l, 4, 0));                  // riga larga scambiata con riga 0
  assert(l.slots[0].span == 2 && l.slots[4].span == 1 && l.slots[5].metric == 3);
  assert(!swap(l, 0, 3));                 // largo verso colonna 1: no
  assert(swap(l, 2, 7));
  assert(validate(l, false));
  Layout bad = l; bad.slots[3].span = 2; bad.slots[2].metric = 99; bad.version = 1;
  assert(!validate(bad, true) && validate(bad, false));
  Layout v0 = l; v0.version = 7; validate(v0, true); assert(v0.slots[0].metric == 0 && v0.version == 1);
  printf("OK test layout PERSONALE\n");
}
