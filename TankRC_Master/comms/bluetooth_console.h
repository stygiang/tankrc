#pragma once

#include <Arduino.h>

#if defined(ARDUINO_ARCH_ESP32) && defined(CONFIG_BT_ENABLED) && CONFIG_BT_ENABLED && defined(CONFIG_BLUEDROID_ENABLED) && CONFIG_BLUEDROID_ENABLED
#include <BluetoothSerial.h>
#define TANKRC_BLUETOOTH_SUPPORTED 1
#else
#define TANKRC_BLUETOOTH_SUPPORTED 0
#endif

namespace TankRC::Config {
struct RuntimeConfig;
}

namespace TankRC::Comms {
class BluetoothConsole {
  public:
    void begin(const Config::RuntimeConfig& config);
    void loop();
    bool connected() const;

  private:
#if TANKRC_BLUETOOTH_SUPPORTED
    BluetoothSerial serial_;
    String buffer_;
#endif
    bool started_ = false;
    bool connected_ = false;
};
}  // namespace TankRC::Comms
