#pragma once
/**
 * @file ui_v2.h
 * @brief Interfaccia v2 "Racing": stessa logica dati della Classica, grafica
 *        nuova pensata per alti FPS. Selezionabile da Impostazioni.
 */
#include <cstdint>

struct _lv_obj_t;
typedef struct _lv_obj_t lv_obj_t;

namespace abarth::ui_v2 {

/// Costruisce e carica lo schermo della UI v2 (prende il lock LVGL).
void build();

/// Pagine della barra in basso
enum class Page : uint8_t { Guida = 0, Temperature, Pressioni, Elettrico, Personale, Setup, Count };

/// Mostra una pagina (da chiamare nel task LVGL)
void showPage(Page p);

}  // namespace abarth::ui_v2
