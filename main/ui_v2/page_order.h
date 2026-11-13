#pragma once
/**
 * @file page_order.h
 * @brief Ordine e visibilita' delle pagine dati della UI Moderno (GUIDA,
 *        TEMPERATURE, PRESSIONI, ELETTRICO, PERSONALE). IMPOSTAZIONI non fa
 *        parte dell'elenco: resta sempre ultima. Salvato in NVS (versionato).
 */
#include <cstdint>

namespace abarth::ui_v2::page_order {

constexpr int kData = 5;              ///< pagine dati (indici 0..4 come ui_v2::Page)
constexpr uint8_t kVersion = 1;

struct Order {
    uint8_t version = kVersion;
    uint8_t order[kData] = {0, 1, 2, 3, 4};   ///< posizione -> pagina
    uint8_t hidden = 0;                       ///< bit = pagina nascosta
    uint8_t reserved = 0;
};

Order defaults();
/// Corregge dati non coerenti (non una permutazione, tutte nascoste...);
/// ritorna true se erano gia' validi
bool validate(Order& o);
bool isHidden(const Order& o, int page);
/// Pagine visibili nell'ordine scelto; ritorna quante sono (>= 1)
int visible(const Order& o, uint8_t* out);
/// Sposta la pagina in posizione pos su/giu'; false se non si puo'
bool moveUp(Order& o, int pos);
bool moveDown(Order& o, int pos);
/// Mostra/nasconde; false se si cerca di nascondere l'ultima visibile
bool setVisible(Order& o, int page, bool on);

Order load();
bool save(const Order& o);

}  // namespace abarth::ui_v2::page_order
