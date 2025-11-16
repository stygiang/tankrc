#pragma once

#include <cstdint>

struct TwoWire;

namespace TankRC::Drivers {
class Pca9548a {
  public:
    void configure(std::uint8_t address = 0x70, TwoWire* wire = nullptr);
    bool selectChannel(std::uint8_t channel);
    void disable();

  private:
    bool writeMask(std::uint8_t mask);

    TwoWire* wire_ = nullptr;
    std::uint8_t address_ = 0x70;
    std::uint8_t currentMask_ = 0;
    bool configured_ = false;
};
}
