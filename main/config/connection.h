#pragma once
#include "config/settings.h"
namespace abarth::connection {
/// Cambia modalita' di collegamento (OBD BLE / OBD WiFi / CAN diretto) e
/// avvia/ferma i servizi giusti. Usata da entrambe le interfacce.
void switchMode(settings::ConnectionMode mode);
}
