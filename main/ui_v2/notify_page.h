#pragma once
/**
 * @file notify_page.h
 * @brief Centro notifiche della UI Moderno: campanella in basso a sinistra con
 *        il numero di avvisi non letti, pannello con la cronologia (dal piu'
 *        recente, per giorno), "Segna tutte lette" e "Cancella cronologia".
 */
#include <cstdint>
struct _lv_obj_t;
typedef struct _lv_obj_t lv_obj_t;

namespace abarth::ui_v2::notify_page {
void buildBell(lv_obj_t* scr, int32_t x, int32_t y, int32_t w, int32_t h, int32_t page_top, int32_t page_bottom);
/// Aggiorna il numero sulla campanella (solo se la cronologia e' cambiata)
void updateBadge();
/// Nasconde la campanella (es. nell'editor di PERSONALE, quando in basso c'e' il suggerimento)
void setBellVisible(bool on);
void open();
void close();
bool isOpen();
}
