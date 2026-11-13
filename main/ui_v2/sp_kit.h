#pragma once
/**
 * @file sp_kit.h
 * @brief Mattoni delle Impostazioni della UI Moderno: pila di sottopagine con
 *        intestazione "‹ Titolo", gruppi su schede, righe ad altezza fissa,
 *        scelte, conferme, messaggi e tastiera. Solo presentazione: le azioni
 *        chiamano le funzioni esistenti (settings, obd, can, sd, rtc...).
 */
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

struct _lv_obj_t;
typedef struct _lv_obj_t lv_obj_t;

namespace abarth::ui_v2::sp {

using Fn = std::function<void()>;
using TextFn = std::function<std::string()>;

/// Da chiamare una volta: pagina che ospita la pila (area contenuti di SETUP)
void init(lv_obj_t* host, int32_t w, int32_t h);
/// Profondita' della pila (0 = elenco categorie); con >0 lo swipe tra pagine e' disattivato
int depth();

/// Apre una sottopagina con titolo e freccia indietro; ritorna la lista
lv_obj_t* push(const char* title);
/// Pagina radice (senza intestazione, non si chiude): l'elenco delle categorie
lv_obj_t* pushRoot();
/// Torna indietro di una pagina
void pop();
/// Chiude tutte le sottopagine
void popAll();

// --- contenuto della lista (si aggiunge in ordine) ------------------------
void section(lv_obj_t* list, const char* title);
void rowNav(lv_obj_t* list, const char* title, TextFn value, Fn on_tap);
void rowValue(lv_obj_t* list, const char* title, TextFn value, Fn on_tap = nullptr);
void rowToggle(lv_obj_t* list, const char* title, std::function<bool()> get, std::function<void(bool)> set);
void rowStepper(lv_obj_t* list, const char* title, TextFn value, Fn minus, Fn plus);
void rowAction(lv_obj_t* list, const char* title, Fn on_tap, bool destructive = false);
/// Riga libera di altezza h nella scheda corrente, per contenuti disegnati a mano
/// (statici); larghezza = rowWidth()
lv_obj_t* rowCustom(lv_obj_t* list, int32_t h);
int32_t rowWidth();
/// Testo su piu' righe aggiornato periodicamente
void rowInfo(lv_obj_t* list, TextFn text);
/// Slider 0..100 (anteprima durante il trascinamento, commit al rilascio)
void rowSlider(lv_obj_t* list, const char* title, int min_v, int max_v, std::function<int()> get,
               std::function<void(int)> preview, std::function<void(int)> commit);

/// Pagina di scelta singola (righe con ✓); set chiamato al tocco, poi torna indietro
void choice(const char* title, const std::vector<std::string>& options, std::function<int()> get,
            std::function<void(int)> set);

/// Finestra di conferma (non blocca il resto del sistema)
void confirm(const char* title, const std::string& text, const char* ok_label, Fn on_ok, bool destructive);
/// Messaggio con un solo pulsante OK
void message(const char* title, const std::string& text);
/// Inserimento testo con tastiera
void textInput(const char* title, const std::string& initial, bool password,
               std::function<void(const std::string&)> done);

/// Aggiorna i valori visibili (chiamata dal timer della pagina SETUP)
void refresh();
/// Lista della pagina in cima alla pila
lv_obj_t* topList();

}  // namespace abarth::ui_v2::sp
