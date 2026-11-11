#pragma once
/**
 * @file widgets.h
 * @brief Componenti grafici leggeri della UI v2.
 *
 * Tutti fatti di rettangoli piatti e label: ogni aggiornamento tocca solo
 * gli oggetti che cambiano davvero (LVGL ridisegna solo quelle aree).
 */
#include <cstdint>
#include "lvgl.h"

namespace abarth::ui_v2::w {

/// Rettangolo piatto e opaco senza stili di default.
lv_obj_t* box(lv_obj_t* parent, int32_t x, int32_t y, int32_t wdt, int32_t hgt, uint32_t bg);

/// Label con font e colore.
lv_obj_t* label(lv_obj_t* parent, const char* txt, const lv_font_t* font, uint32_t color);

/// Imposta testo/colore solo se cambiano (nessuna invalidazione inutile).
void setText(lv_obj_t* lbl, const char* txt);
void setColor(lv_obj_t* lbl, uint32_t color, uint32_t& cache);

/// Display numerico a 7 segmenti fatto di rettangoli.
class SevenSeg {
public:
    static constexpr int kMaxDigits = 5;
    void create(lv_obj_t* parent, int32_t x, int32_t y, int digits, int32_t dw, int32_t dh,
                int32_t thick, int32_t gap, uint32_t on_color, uint32_t off_color);
    /// value < 0 = trattini (dato non disponibile). Zeri iniziali spenti.
    void set(int32_t value);
    void setOnColor(uint32_t color);
    int32_t width() const { return width_; }

private:
    void applyDigit(int d, uint8_t mask);
    lv_obj_t* seg_[kMaxDigits][7] = {};
    uint8_t   mask_[kMaxDigits] = {};
    int       digits_ = 0;
    int32_t   last_ = -2;
    int32_t   width_ = 0;
    uint32_t  on_ = 0, off_ = 0;
};

/// Barra a segmenti orizzontale (contagiri / shift-light).
class SegBar {
public:
    static constexpr int kMaxSeg = 48;
    void create(lv_obj_t* parent, int32_t x, int32_t y, int32_t wdt, int32_t hgt, int segments,
                int32_t gap, float yellow_from, float red_from);
    /// fraction 0..1; flash = tutti i segmenti accesi in rosso (limitatore)
    void set(float fraction, bool flash, bool flash_phase);

private:
    lv_obj_t* seg_[kMaxSeg] = {};
    uint32_t  col_on_[kMaxSeg] = {};
    uint32_t  shown_[kMaxSeg] = {};
    int       n_ = 0;
};

/// Barra orizzontale semplice (traccia + riempimento), opzionale con zero centrale.
class HBar {
public:
    void create(lv_obj_t* parent, int32_t x, int32_t y, int32_t wdt, int32_t hgt,
                float min_v, float max_v, uint32_t color);
    void set(float v, bool valid);
    void setColor(uint32_t color);

private:
    lv_obj_t* fill_ = nullptr;
    int32_t   x0_ = 0, w_ = 0, zero_px_ = 0, last_a_ = -1, last_b_ = -1;
    float     min_ = 0, max_ = 1;
    uint32_t  color_ = 0;
};

/// Riquadro valore: titolo piccolo, valore grande, unita'.
class Tile {
public:
    void create(lv_obj_t* parent, int32_t x, int32_t y, int32_t wdt, int32_t hgt,
                const char* title, const lv_font_t* value_font);
    void set(const char* value, const char* unit, uint32_t color);
    lv_obj_t* obj() const { return box_; }
    int32_t w() const { return w_; }
    int32_t h() const { return h_; }

private:
    lv_obj_t* box_ = nullptr;
    lv_obj_t* value_ = nullptr;
    lv_obj_t* unit_ = nullptr;
    uint32_t  color_ = 0xFFFFFFFF;
    int32_t   w_ = 0, h_ = 0;
};

}  // namespace abarth::ui_v2::w
