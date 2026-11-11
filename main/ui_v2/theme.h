#pragma once
/**
 * @file theme.h
 * @brief Stile della UI v2 "Moderno" (design C "Schede"): tutti i colori,
 *        i font e le misure in un solo punto, cosi' l'aspetto si cambia qui.
 *
 * Regole di prestazione:
 *  - schede e testi statici disegnati una volta; si invalidano solo valori,
 *    riempimenti delle barre e pallini di stato;
 *  - niente ombre, trasparenze, sfumature; schede opache senza bordo;
 *  - le aree dei valori non toccano gli angoli arrotondati delle schede.
 */
#include <cstdint>
#include "lvgl.h"

namespace abarth::ui_v2::theme {

// --- Colori ------------------------------------------------------------------
constexpr uint32_t kBg        = 0x0A0B0E;  ///< sfondo schermo
constexpr uint32_t kCard      = 0x14161B;  ///< schede
constexpr uint32_t kCardPress = 0x1B1E24;  ///< scheda/riga premuta
constexpr uint32_t kText      = 0xF4F5F7;  ///< testo principale
constexpr uint32_t kDim       = 0x5D606A;  ///< etichette e unita'
constexpr uint32_t kSecondary = 0xC9CBD1;  ///< testo secondario
constexpr uint32_t kTrack     = 0x23262D;  ///< traccia barre
constexpr uint32_t kRowLine   = 0x1D1F24;  ///< separatore righe impostazioni
constexpr uint32_t kDotIdle   = 0x2B2D33;  ///< pallini indicatore pagina
constexpr uint32_t kOk        = 0x3ECF8E;
constexpr uint32_t kWarn      = 0xFFB547;
constexpr uint32_t kCrit      = 0xE5322D;  ///< critico / zona rossa

/// Colore accento scelto in Impostazioni (0 bianco, 1 rosso, 2 azzurro, 3 ambra)
uint32_t accent();
constexpr uint32_t kAccents[4] = {0xF2F3F5, 0xE5322D, 0x7FD4FF, 0xFFB547};
constexpr const char* kAccentNames[4] = {"Bianco", "Rosso Abarth", "Azzurro ghiaccio", "Ambra"};

// --- Misure (portrait 600x1024) ---------------------------------------------
constexpr int32_t kMargin    = 24;   ///< margine laterale schede
constexpr int32_t kGap       = 18;   ///< spazio tra schede
constexpr int32_t kCardPad   = 30;   ///< padding interno schede
constexpr int32_t kRadius    = 27;   ///< raggio schede
constexpr int32_t kStatusTop = 19;   ///< y testo barra di stato
constexpr int32_t kStatusX   = 30;   ///< margine testo barra di stato
constexpr int32_t kCardsTop  = 66;   ///< inizio area schede
constexpr int32_t kCardsBot  = 970;  ///< fine area schede
constexpr int32_t kDotsY     = 1000; ///< indicatore pagina (centro)
constexpr int32_t kPillH     = 13;   ///< barra a pillola
constexpr int32_t kStatusDot = 12;   ///< pallino stato scheda
constexpr int32_t kRowH      = 72;   ///< righe impostazioni

// --- Font (ruoli) -------------------------------------------------------------
// Provvisori Montserrat finche' non c'e' Manrope generato con lv_font_conv.
const lv_font_t* fontHero();      ///< cifre principali (~228 px, peso 300)
const lv_font_t* fontValue();     ///< valori schede (75 px, peso 400)
const lv_font_t* fontMedium();    ///< valori medi (32 px, peso 500)
const lv_font_t* fontUnit();      ///< unita' (22 px, peso 500)
const lv_font_t* fontLabel();     ///< etichette MAIUSCOLE (17 px, peso 500)
const lv_font_t* fontStatus();    ///< barra di stato (19 px)
const lv_font_t* fontBody();      ///< testo impostazioni (24 px, peso 500)
constexpr int32_t kLabelLetterSpace = 3;

inline lv_color_t c(uint32_t hex) { return lv_color_hex(hex); }

}  // namespace abarth::ui_v2::theme
