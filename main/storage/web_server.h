#pragma once

#include <cstdint>
#include <string>
#include "esp_err.h"

namespace abarth::web {

/// Avvia il Web Server HTTP su porta 80
esp_err_t startServer();

/// Arresta il Web Server HTTP
void stopServer();

/// Ritorna true se il Web Server è attualmente in esecuzione
bool isRunning();

/// Solo test: avvio anche senza rete (stack TCP/IP gia' inizializzato)
esp_err_t debugStartWithoutWifi();
/// Solo test: durata dell'arresto automatico (0 = predefinito 10 minuti)
void debugSetAutoStopMs(uint32_t ms);

/// Ritorna l'indirizzo IP locale da mostrare nelle impostazioni o nei log
std::string getServerUrl();

}  // namespace abarth::web
