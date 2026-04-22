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

/// Intervallo tra un ciclo di polling e il successivo, in ms.
inline constexpr uint32_t kPollIntervalMs           = 50;

/// Timeout risposta ELM327 per singolo comando, in ms.
inline constexpr uint32_t kElmResponseTimeoutMs     = 1500;

/// Numero massimo di PID consecutivi "NO DATA" prima di considerarli non
/// supportati (temporaneamente) e rallentarne il polling.
inline constexpr uint8_t  kMaxNoDataBeforeBackoff   = 5;

/* --- UI ------------------------------------------------------------------- */

/// FPS target per LVGL.
inline constexpr uint32_t kLvglRefreshMs            = 16;   ///< ~60 FPS

/// Aggiornamento dei widget numerici (evita flickering).
inline constexpr uint32_t kUiUpdateMs               = 100;  ///< 10 Hz

}  // namespace abarth::config
