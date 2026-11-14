#pragma once
#include "config/settings.h"
namespace abarth::connection {
/// Cambia modalita' di collegamento (OBD BLE / OBD WiFi / CAN diretto) e
/// avvia/ferma i servizi giusti. Usata da entrambe le interfacce.
void switchMode(settings::ConnectionMode mode);
/// Dopo l'avvio: se la modalita' non e' OBD Wi-Fi, si collega da sola
/// all'ultima rete "Internet" salvata (se e' nei paraggi)
void autoConnectInternetWifi();
}
