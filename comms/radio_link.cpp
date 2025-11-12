#include <Arduino.h>

#include "comms/radio_link.h"

namespace TankRC::Comms {
void RadioLink::begin() {
    // Placeholder for ESP-NOW, RC receiver, or Bluetooth initialization.
}

CommandPacket RadioLink::poll() {
    CommandPacket packet;
    if (Serial.available()) {
        // Consume debug bytes to avoid blocking when nothing is implemented yet.
        Serial.read();
    }
    return packet;
}
}  // namespace TankRC::Comms
