#pragma once
/**
 * @file custom_layout.h
 * @brief Modello della pagina PERSONALE: griglia fissa di slot configurabili.
 *
 * Griglia 2 colonne x 4 righe (8 slot, indice = riga*2 + colonna).
 * Ogni slot: grandezza (o vuoto), tipo di widget, larghezza (1 o 2 colonne).
 * Uno slot largo (span 2) puo' stare solo nella colonna 0 e "copre" lo slot
 * a destra, che deve restare vuoto.
 * Salvato in NVS come blob versionato; se non valido si usa il layout di default.
 * Questo file non dipende dallo stile grafico.
 */
#include <cstdint>

namespace abarth::ui_v2::custom {

constexpr int kCols = 2;
constexpr int kRows = 4;
constexpr int kSlots = kCols * kRows;
constexpr uint8_t kEmpty = 0xFF;        ///< slot senza grandezza
constexpr uint8_t kLayoutVersion = 1;

enum class WidgetType : uint8_t {
    BigNumber = 0,   ///< numero grande
    NumberBar = 1,   ///< numero + barra orizzontale
    PeakMinMax = 2,  ///< valore con minimo e massimo della sessione
    Count
};

struct Slot {
    uint8_t metric = kEmpty;   ///< valore di data::Metric o kEmpty
    uint8_t type = 0;          ///< WidgetType
    uint8_t span = 1;          ///< 1 = una colonna, 2 = riga intera
    uint8_t reserved = 0;
};

struct Layout {
    uint8_t version = kLayoutVersion;
    uint8_t reserved[3] = {};
    Slot    slots[kSlots];
};

/// Layout iniziale proposto (giri, velocita', turbo, acqua, olio, batteria...)
Layout defaultLayout();

/// Controlla coerenza (versione, grandezze esistenti, tipi, span). Corregge
/// solo se `fix` e' true; ritorna true se il layout era gia' valido.
bool validate(Layout& l, bool fix);

// --- Operazioni dell'editor (pure, verificabili su PC) ----------------------
/// Assegna una grandezza (o kEmpty) a uno slot. Ignora gli slot coperti.
bool setMetric(Layout& l, int slot, uint8_t metric);
/// Cambia il tipo di widget.
bool setType(Layout& l, int slot, WidgetType t);
/// Cambia larghezza: span 2 solo in colonna 0; svuota lo slot a destra.
bool setSpan(Layout& l, int slot, uint8_t span);
/// Scambia il contenuto di due slot rispettando i vincoli di larghezza.
bool swap(Layout& l, int a, int b);
/// true se lo slot e' coperto da uno slot largo alla sua sinistra.
bool isCovered(const Layout& l, int slot);

// --- Persistenza (solo su ESP32) --------------------------------------------
/// Carica da NVS; se manca o non e' valido ritorna il default.
Layout load();
/// Salva in NVS (da chiamare solo all'uscita dall'editor).
bool save(const Layout& l);

}  // namespace abarth::ui_v2::custom
