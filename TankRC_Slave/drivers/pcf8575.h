#pragma once

#include <cstdint>
#include <Wire.h>

namespace TankRC::Drivers {
class Pcf8575 {
  public:
    bool begin(std::uint8_t address = 0x20, TwoWire* wire = nullptr);
    void writePin(int index, bool high);
    bool ready() const { return ready_; }

  private:
    bool flush();

    TwoWire* wire_ = &Wire;
    std::uint8_t address_ = 0x20;
    bool ready_ = false;
    std::uint16_t state_ = 0xFFFF;
};
}  // namespace TankRC::Drivers
