#pragma once
/**
 * @file theme.h
 * @brief Palette e costanti della UI v2 "Racing".
 *
 * Regole di prestazione (vedi README della v2):
 *  - solo sfondi piatti e opachi, niente ombre, sfumature, trasparenze;
 *  - raggi solo su elementi piccoli, nessun contenitore annidato con bordi;
 *  - posizioni e dimensioni fisse (niente flex/grid ricalcolati);
 *  - i valori si aggiornano solo quando cambia il testo mostrato.
 */
#include <cstdint>
#include "lvgl.h"

namespace abarth::ui_v2::theme {

// Colori
constexpr uint32_t kBg        = 0x08080A;  ///< sfondo schermo
constexpr uint32_t kPanel     = 0x131317;  ///< riquadri
constexpr uint32_t kPanelHi   = 0x1C1C22;  ///< riquadro premuto / selezionato
constexpr uint32_t kLine      = 0x2A2A31;  ///< linee sottili
constexpr uint32_t kText      = 0xFFFFFF;
constexpr uint32_t kMuted     = 0x8C8C96;
constexpr uint32_t kRed       = 0xE10600;  ///< rosso Abarth
constexpr uint32_t kYellow    = 0xFFC400;  ///< avvisi
constexpr uint32_t kGreen     = 0x2ED573;
constexpr uint32_t kBlue      = 0x3AB6FF;  ///< motore freddo
constexpr uint32_t kSegOff    = 0x1E1E24;  ///< segmento barra spento

// Geometria
constexpr int32_t kStatusH = 44;   ///< barra di stato in alto
constexpr int32_t kNavH    = 76;   ///< barra pagine in basso
constexpr int32_t kGap     = 8;    ///< spazio tra riquadri
constexpr int32_t kPad     = 12;   ///< margine interno riquadri

// Font (pochi tagli)
inline const lv_font_t* fontSmall()  { return &lv_font_montserrat_14; }
inline const lv_font_t* fontLabel()  { return &lv_font_montserrat_16; }
inline const lv_font_t* fontMedium() { return &lv_font_montserrat_24; }
inline const lv_font_t* fontValue()  { return &lv_font_montserrat_36; }
inline const lv_font_t* fontHuge()   { return &lv_font_montserrat_48; }

inline lv_color_t c(uint32_t hex) { return lv_color_hex(hex); }

}  // namespace abarth::ui_v2::theme
