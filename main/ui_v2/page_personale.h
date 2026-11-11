#pragma once
#include <cstdint>
struct _lv_obj_t;
typedef struct _lv_obj_t lv_obj_t;
/// Pagina PERSONALE: griglia di schede configurabile dall'utente (editor integrato).
namespace abarth::ui_v2::personale {
void build(lv_obj_t* parent, int32_t w, int32_t h, int32_t y0);
void update(int64_t now_ms);
/// true se l'editor e' aperto (lo swipe tra pagine e' disattivato)
bool editing();
/// Solo debug/screenshot: 0 = esci, 1 = editor con scheda 3 selezionata, 2 = elenco grandezze
void debugMode(int mode);
}
