#pragma once
/**
 * @file background.h
 * @brief Sfondi della UI Moderno: disegnati una sola volta in un'immagine
 *        RGB565 600x1024 in PSRAM (sotto alle pagine, fissa: non scorre).
 *        Le schede restano opache sopra, quindi durante l'uso si ridisegnano
 *        solo gli spazi tra le schede. Contrasto molto basso (pochi %).
 */
#include <cstdint>
struct _lv_obj_t;
typedef struct _lv_obj_t lv_obj_t;

namespace abarth::ui_v2::background {

enum Style : uint8_t { None = 0, Carbon, Vignette, Lines, Scorpion, ScorpionOutline, Wordmark, Count };

/// Nomi per le Impostazioni (ordine di Style)
extern const char* const kNames[Count];

/// Crea l'immagine di sfondo come primo figlio dello schermo e applica lo stile salvato
void create(lv_obj_t* scr);
/// Cambia sfondo subito (nel task LVGL)
void apply(uint8_t style);

}  // namespace abarth::ui_v2::background
