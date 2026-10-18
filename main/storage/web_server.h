#pragma once

#include <string>
#include "esp_err.h"

namespace abarth::web {

/// Avvia il Web Server HTTP su porta 80
esp_err_t startServer();

/// Arresta il Web Server HTTP
void stopServer();

/// Ritorna true se il Web Server è attualmente in esecuzione
bool isRunning();

/// Ritorna l'indirizzo IP locale da mostrare nelle impostazioni o nei log
std::string getServerUrl();

}  // namespace abarth::web
