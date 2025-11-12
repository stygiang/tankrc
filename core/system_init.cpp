#include <Arduino.h>

#include "config/pins.h"
#include "core/system_init.h"

namespace TankRC::Core {
void setupHardware() {
    pinMode(Pins::STATUS_LED, OUTPUT);
}

void serviceWatchdog() {
    digitalWrite(Pins::STATUS_LED, !digitalRead(Pins::STATUS_LED));
}
}  // namespace TankRC::Core
