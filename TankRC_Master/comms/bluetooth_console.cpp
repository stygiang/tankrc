#include "comms/bluetooth_console.h"

#include "ui/console.h"

namespace TankRC::Comms {
void BluetoothConsole::begin(const Config::RuntimeConfig& config) {
(void)config;
#if TANKRC_BLUETOOTH_SUPPORTED
    if (started_) {
        return;
    }
    if (!serial_.begin("TankRC Console")) {
        Serial.println(F("[BT] Failed to start Bluetooth serial console."));
        return;
    }
    started_ = true;
    connected_ = false;
    buffer_.clear();
    Serial.println(F("[BT] Bluetooth serial console started (TankRC Console)."));
#else
    Serial.println(F("[BT] Bluetooth console unavailable on this target."));
#endif
}

void BluetoothConsole::loop() {
#if TANKRC_BLUETOOTH_SUPPORTED
    if (!started_) {
        return;
    }

    const bool hasClient = serial_.hasClient();
    if (hasClient && !connected_) {
        connected_ = true;
        UI::addConsoleTap(&serial_);
        serial_.println();
        serial_.println(F("TankRC Bluetooth console ready. Type 'help' for commands."));
        serial_.print(F("> "));
    } else if (!hasClient && connected_) {
        connected_ = false;
        UI::removeConsoleTap(&serial_);
        buffer_.clear();
    }

    if (!hasClient) {
        return;
    }

    while (serial_.available()) {
        char c = static_cast<char>(serial_.read());
        if (c == '\r') {
            continue;
        }
        if (c == '\n') {
            String line = buffer_;
            buffer_.clear();
            UI::injectRemoteLine(line, UI::ConsoleSource::Bluetooth);
        } else {
            buffer_ += c;
        }
    }
#else
    (void)connected_;
#endif
}

bool BluetoothConsole::connected() const {
#if TANKRC_BLUETOOTH_SUPPORTED
    return connected_;
#else
    return false;
#endif
}
}  // namespace TankRC::Comms
