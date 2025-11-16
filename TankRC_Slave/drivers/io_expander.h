#pragma once

#include <cstdint>

#include "drivers/pca9548a.h"

struct TwoWire;

namespace TankRC::Drivers {

class IoExpander {
  public:
    void configure(std::uint8_t address,
                   bool useMux,
                   std::uint8_t muxAddress,
                   std::uint8_t muxChannel);
    bool begin(TwoWire* wire = nullptr);
    void pinMode(std::uint8_t pin, bool output);
    void digitalWrite(std::uint8_t pin, bool high);
    bool ready() const { return ready_; }

  private:
    bool ensureBusSelected() const;
    bool writeRegister(std::uint8_t reg, std::uint16_t value) const;
    bool readRegister(std::uint8_t reg, std::uint16_t& value) const;
    bool updateOutputs() const;
    bool updateDirection() const;

    TwoWire* wire_ = nullptr;
    std::uint8_t address_ = 0x20;
    bool useMux_ = false;
    std::uint8_t muxAddress_ = 0x70;
    std::uint8_t muxChannel_ = 0;
    mutable bool ready_ = false;
    mutable std::uint16_t outputs_ = 0;
    mutable std::uint16_t direction_ = 0xFFFF;
    mutable Pca9548a mux_{};
    mutable bool outputsDirty_ = false;
    mutable bool directionDirty_ = false;
};
}
