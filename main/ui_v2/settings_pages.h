#pragma once
#include <cstdint>
struct _lv_obj_t;
typedef struct _lv_obj_t lv_obj_t;
/// Impostazioni complete della UI Moderno (tutte le funzioni della Classica).
namespace abarth::ui_v2::settings_pages {
void build(lv_obj_t* host, int32_t w, int32_t h);
void update(int64_t now_ms);
/// true se e' aperta una sottopagina (lo swipe tra pagine resta disattivato)
bool inSubPage();
/// Lista scorrevole delle categorie (benchmark)
lv_obj_t* rootList();
/// Solo debug/screenshot: apre la categoria n (0..10)
void debugOpen(int n);
/// Solo debug: scorre la sottopagina aperta in fondo; ritorna false se non scorre
bool debugScrollBottom();
int debugDepth();
lv_obj_t* debugTopList();
}
