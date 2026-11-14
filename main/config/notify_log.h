#pragma once
/**
 * @file notify_log.h
 * @brief Centro notifiche: cronologia degli avvisi comparsi (anche quelli
 *        nascosti nelle Impostazioni). Due registri separati che non si
 *        mescolano: avvisi reali e avvisi nati durante la simulazione dati.
 *        Fino a 200 voci per registro (le piu' vecchie si sovrascrivono),
 *        salvate su microSD (/sdcard/notifiche*.bin) oppure, senza scheda, le
 *        ultime 20 in NVS. Il salvataggio avviene in un task a parte.
 */
#include <cstdint>
#include <vector>

namespace abarth::notify {

constexpr int kMax = 200;

struct Entry {
    uint32_t epoch = 0;     ///< data e ora (0 = orologio non impostato)
    uint8_t level = 0;      ///< 0 info, 1 avviso, 2 critico
    uint8_t read = 0;
    uint8_t pad[2] = {};
    char title[40] = "";
    char msg[112] = "";
};

/// Carica i registri (dopo l'avvio della microSD)
void init();
/// Registra un avviso; finisce nel registro simulato se la simulazione e' attiva
void add(uint8_t level, const char* title, const char* msg);
/// Voci del registro in uso (simulato se la simulazione e' attiva), dalla piu' recente
std::vector<Entry> list();
/// Non lette nel registro in uso
int unread();
void markAllRead();
void clear();
/// true se il registro mostrato e' quello della simulazione
bool showingSimulated();
/// Incrementato a ogni modifica (per aggiornare l'interfaccia solo se serve)
uint32_t revision();

}  // namespace abarth::notify
