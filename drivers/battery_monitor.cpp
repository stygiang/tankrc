#include <Arduino.h>

#include "drivers/battery_monitor.h"

namespace TankRC::Drivers {
void BatteryMonitor::attach(int analogPin, float scale) {
    analogPin_ = analogPin;
    scale_ = scale;
    if (analogPin_ >= 0) {
        pinMode(analogPin_, INPUT);
    }
}

float BatteryMonitor::readVoltage() const {
    if (analogPin_ < 0) {
        return 0.0F;
    }
    const float raw = static_cast<float>(analogRead(analogPin_));
    const float voltage = (raw / 4095.0F) * 3.3F * scale_;
    return voltage;
}
}  // namespace TankRC::Drivers
