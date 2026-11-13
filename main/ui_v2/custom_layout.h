#pragma once
/**
 * @file custom_layout.h
 * @brief Modello della pagina PERSONALE (versione 2): elenco ordinato di schede
 *        su una griglia fine di 4 colonne, disposte in ordine "riempiendo i buchi";
 *        la pagina scorre in verticale se le schede non ci stanno.
 *
 * Dimensioni: larghezza 1, 2 o 4 colonne (1/4, 1/2, intera) e altezza 1 o 2
 * righe (bassa, alta). Es.: 0.25x = 1/4 bassa, 0.5x = 1/2 bassa,
 * 1x = 1/2 alta (scheda normale), 2x = intera alta.
 * Salvato in NVS come blob versionato; il formato v1 (griglia 2x4) viene
 * convertito automaticamente. Questo file non dipende dallo stile grafico.
 */
#include <cstdint>

namespace abarth::ui_v2::custom {

constexpr int kCols = 4;                 ///< colonne della griglia fine
constexpr int kMaxCards = 32;
constexpr int kMaxRows = 64;             ///< righe massime della griglia fine
constexpr uint8_t kEmpty = 0xFF;
constexpr uint8_t kLayoutVersion = 2;

enum class WidgetType : uint8_t {
    BigNumber = 0,   ///< numero
    NumberBar = 1,   ///< numero + barra
    PeakMinMax = 2,  ///< valore con minimo e massimo di sessione
    Count
};

struct Card {
    uint8_t metric = 0;     ///< data::Metric
    uint8_t type = 0;       ///< WidgetType
    uint8_t cols = 2;       ///< larghezza in colonne: 1, 2, 4
    uint8_t rows = 2;       ///< altezza in righe: 1, 2
};

struct Layout {
    uint8_t version = kLayoutVersion;
    uint8_t count = 0;      ///< schede usate
    uint8_t flags = 0;      ///< bit0 = suggerimento PERSONALE gia' visto
    uint8_t reserved = 0;
    Card    cards[kMaxCards];
};

struct Placement { uint8_t col, row, cols, rows; };

/// Disposizione: prima posizione libera in ordine di lettura per ogni scheda.
/// Ritorna il numero di righe usate.
int pack(const Layout& l, Placement out[kMaxCards]);

Layout defaultLayout();
/// Coerenza (versione, grandezze, tipi, dimensioni, numero). Con fix=true corregge.
bool validate(Layout& l, bool fix);

// --- Operazioni dell'editor ---------------------------------------------------
int  add(Layout& l, uint8_t metric, WidgetType t, uint8_t cols, uint8_t rows);   ///< indice o -1 se pieno
bool remove(Layout& l, int i);
bool setMetric(Layout& l, int i, uint8_t metric);
bool setType(Layout& l, int i, WidgetType t);
bool setSize(Layout& l, int i, uint8_t cols, uint8_t rows);
/// Tipi consentiti per dimensione: barra e min/max solo da meta' larghezza in su
bool typeAllowed(uint8_t cols, uint8_t rows, WidgetType t);
bool swap(Layout& l, int a, int b);

/// Converte un layout della versione 1 (griglia 2x4 da 8 slot, blob da 36 byte):
/// 1x -> 1/2 alta, 2x -> intera alta
bool migrateV1(const uint8_t* blob, int len, Layout& out);

// --- Persistenza (solo su ESP32) ----------------------------------------------
Layout load();
bool save(const Layout& l);

}  // namespace abarth::ui_v2::custom
