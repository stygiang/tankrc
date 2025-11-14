#pragma once

namespace TankRC::Drivers {
class BatteryMonitor {
  public:
    void attach(int analogPin, float scale = 1.0F);
    float readVoltage() const;

  private:
    int analogPin_ = -1;
    float scale_ = 1.0F;
};
}  // namespace TankRC::Drivers
