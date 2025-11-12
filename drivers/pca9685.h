#pragma once
#ifndef TANKRC_DRIVERS_PCA9685_H
#define TANKRC_DRIVERS_PCA9685_H

#include <cstdint>

struct TwoWire;

namespace TankRC::Drivers {
class Pca9685 {
  public:
    bool begin(std::uint8_t address = 0x40, std::uint16_t frequency = 1000, TwoWire* wire = nullptr);
    void setChannelValue(int channel, std::uint16_t value);
    void setChannelNormalized(int channel, float normalized);

  private:
    void write8(std::uint8_t reg, std::uint8_t value);
    void setPwm(std::uint8_t channel, std::uint16_t on, std::uint16_t off);
    void setFrequency(std::uint16_t freq);

    std::uint8_t address_ = 0x40;
    std::uint16_t frequency_ = 1000;
    TwoWire* wire_ = nullptr;
    bool ready_ = false;
};
}  // namespace TankRC::Drivers
#endif  // TANKRC_DRIVERS_PCA9685_H
