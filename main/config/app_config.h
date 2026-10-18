#pragma once

/**
 * @file app_config.h
 * @brief Configurazione globale dell'applicazione.
 *
 * Modifica qui i parametri che non richiedono ricompilare la logica:
 * nome del dongle OBD, intervallo di polling, ecc.
 */

#include <cstdint>

namespace abarth::config {

/* --- Dongle Bluetooth LE (vLinker FD) ------------------------------------- */

/// Nome broadcast del dongle OBD cercato in scansione.
/// Il vLinker FD di default annuncia "vLinker FD" / "OBDBLE".
/// Puoi sovrascriverlo tramite menu "Impostazioni" (salvato in NVS).
inline constexpr const char kDefaultBleDeviceName[] = "vLinker FD";

/// UUID del servizio BLE usato dalla maggior parte dei dongle cinesi compatibili
/// ELM327 (vLinker FD, OBDLink CX, ecc.). Se il tuo dongle mostra un servizio
/// diverso in nRF Connect, aggiornalo qui.
inline constexpr const char kBleServiceUUID[]       = "FFF0";
inline constexpr const char kBleNotifyCharUUID[]    = "FFF1"; ///< RX (notifiche)
inline constexpr const char kBleWriteCharUUID[]     = "FFF2"; ///< TX (scrittura)

/* --- Polling -------------------------------------------------------------- */

/// Intervallo tra un ciclo di polling e il successivo, in ms (30ms per reattività ~20-30Hz sui PID caldi)
inline constexpr uint32_t kPollIntervalMs           = 30;

/// Timeout risposta ELM327 per PID "caldi" (RPM, velocita', acceleratore,
/// boost, carico, MAP): valori che l'utente guarda in tempo reale.
inline constexpr uint32_t kElmResponseTimeoutFastMs = 400;

/// Timeout risposta ELM327 per PID "lenti" (temperature, livello carburante,
/// tensione, ecc.).
inline constexpr uint32_t kElmResponseTimeoutMs     = 800;

/// Pausa tra un PID caldo e il successivo (brevissima: i caldi sono pochi).
inline constexpr uint32_t kPidCooldownFastMs        = 10;

/// Pausa tra un PID lento e il successivo, in ms. Evita overflow nel buffer
/// del dongle (soprattutto su BLE con MTU limitato).
inline constexpr uint32_t kPidCooldownMs            = 40;

/// Numero massimo di PID "lenti" interrogati in un singolo ciclo di polling,
/// oltre ai PID caldi (che vengono sempre interrogati). 1 = massima priorità
/// ai PID caldi (RPM, Boost) ad ogni ciclo.
inline constexpr uint8_t  kSlowPidsPerCycle         = 1;

/// Numero massimo di fallimenti consecutivi su un PID prima di marcarlo
/// "non supportato". 10 = molto tollerante, evita falsi positivi.
inline constexpr uint8_t  kMaxNoDataBeforeBackoff   = 10;

/// Una volta marcato "non supportato", ogni quanti ms riprovarlo comunque?
/// (cosi' un PID perso transitoriamente puo' tornare attivo). 0 = mai.
inline constexpr uint32_t kNotSupportedRetryMs      = 20000;

/// Numero massimo di fallimenti consecuti su un PID prima di disconnettere
/// il link (presupposto: il dongle BLE e' morto). Soglia alta per evitare
/// false disconnessioni su un PID realmente non supportato.
inline constexpr uint8_t  kMaxFailBeforeDisconnect  = 20;

/* --- UI ------------------------------------------------------------------- */

/// FPS target per LVGL.
inline constexpr uint32_t kLvglRefreshMs            = 16;   ///< ~60 FPS

/// Aggiornamento dei widget numerici (evita flickering).
inline constexpr uint32_t kUiUpdateMs               = 100;  ///< 10 Hz

}  // namespace abarth::config
