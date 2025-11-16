#pragma once

#include <cstdint>

namespace TankRC::Drivers {
class Pcf8575 {
  public:
    bool begin(std::uint8_t address = 0x20);
    void writePin(int index, bool high);
    bool ready() const { return ready_; }

  private:
    bool flush();

    std::uint8_t address_ = 0x20;
    bool ready_ = false;
    std::uint16_t state_ = 0xFFFF;
};
}  // namespace TankRC::Drivers
