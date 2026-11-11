#pragma once
/**
 * @file widgets.h
 * @brief Componenti della UI v2 "Moderno" (schede). Ogni set() tocca solo
 *        cio' che cambia; scheda, etichette e tracce sono statiche.
 */
#include <cstdint>
#include "lvgl.h"
#include "ui_v2/binding.h"

namespace abarth::ui_v2::w {

/// Rettangolo piatto e opaco senza stili di default.
lv_obj_t* box(lv_obj_t* parent, int32_t x, int32_t y, int32_t wdt, int32_t hgt, uint32_t bg, int32_t radius = 0);
/// Scheda (opaca, raggio del tema, nessun bordo).
lv_obj_t* card(lv_obj_t* parent, int32_t x, int32_t y, int32_t wdt, int32_t hgt);
/// Label con font e colore; `upper_label` aggiunge la spaziatura delle etichette.
lv_obj_t* label(lv_obj_t* parent, const char* txt, const lv_font_t* font, uint32_t color, bool upper_label = false);

void setText(lv_obj_t* lbl, const char* txt);
void setColor(lv_obj_t* lbl, uint32_t color, uint32_t& cache);
/// Larghezza in pixel di un testo con un font
int32_t textWidth(const char* txt, const lv_font_t* font);

/// Colore del livello (ok/avviso/critico) per pallini e valori
uint32_t levelColor(Level lv);

/// Barra a pillola: traccia statica + riempimento che cambia larghezza.
class PillBar {
public:
    void create(lv_obj_t* parent, int32_t x, int32_t y, int32_t wdt);
    /// fraction 0..1 (NaN = vuota)
    void set(float fraction, uint32_t color);
private:
    lv_obj_t* fill_ = nullptr;
    int32_t   w_ = 0, last_w_ = -1;
    uint32_t  color_ = 0;
};

/// Pallino di stato (nascosto se il dato manca o la grandezza non ha soglie)
class StatusDot {
public:
    void create(lv_obj_t* parent, int32_t x, int32_t y);
    void set(bool visible, uint32_t color);
    lv_obj_t* obj() const { return dot_; }
private:
    lv_obj_t* dot_ = nullptr;
    int       vis_ = -1;
    uint32_t  color_ = 0;
};

/// Scheda valore piccola: etichetta, pallino, valore + unita' in basso.
class ValueCard {
public:
    /// show_dot: true per grandezze con soglie d'allarme
    void create(lv_obj_t* parent, int32_t x, int32_t y, int32_t wdt, int32_t hgt,
                const char* title, bool show_dot);
    /// Aggiorna dai dati (solo se cambiati); dot_level: livello per il pallino
    void set(const Binding& b, Level dot_level);
    lv_obj_t* obj() const { return card_; }
private:
    lv_obj_t* card_ = nullptr;
    lv_obj_t* value_ = nullptr;
    lv_obj_t* unit_ = nullptr;
    StatusDot dot_;
    bool      has_dot_ = false;
    uint32_t  value_color_ = 0;
    int32_t   value_x_ = 0, value_y_ = 0;
};

/// Scheda larga: etichetta, pallino, valore + unita' e barra a pillola in basso.
class WideCard {
public:
    void create(lv_obj_t* parent, int32_t x, int32_t y, int32_t wdt, int32_t hgt,
                const char* title, bool show_dot);
    void set(const Binding& b, Level dot_level);
    lv_obj_t* obj() const { return card_; }
private:
    lv_obj_t* card_ = nullptr;
    lv_obj_t* value_ = nullptr;
    lv_obj_t* unit_ = nullptr;
    StatusDot dot_;
    PillBar   bar_;
    bool      has_dot_ = false;
    uint32_t  value_color_ = 0;
    int32_t   value_y_ = 0;
};

}  // namespace abarth::ui_v2::w
