#include "config/connection.h"
#include "can/can_manager.h"
#include "obd/obd_manager.h"

namespace abarth::connection {
void switchMode(settings::ConnectionMode target) {
    if (target == settings::current().connection_mode) return;
    settings::setConnectionMode(target);
    if (target == settings::ConnectionMode::DirectCan) {
        // Ferma l'OBD senza cancellare dongle/rete salvati
        obd::obd().stopService();
        can::can().start();
    } else if (target == settings::ConnectionMode::BleObd) {
        can::can().stop();
        obd::obd().connectBLE();
    } else if (target == settings::ConnectionMode::WifiObd) {
        can::can().stop();
        obd::obd().connectWiFi();
    }
}
}  // namespace abarth::connection
