#include "drivers/pca9548a.h"

#include <Wire.h>

namespace TankRC::Drivers {
void Pca9548a::configure(std::uint8_t address, TwoWire* wire) {
    address_ = address;
    wire_ = wire ? wire : &Wire;
    currentMask_ = 0;
    configured_ = wire_ != nullptr;
}

bool Pca9548a::selectChannel(std::uint8_t channel) {
    if (!configured_ || !wire_) {
        return false;
    }
    if (channel > 7) {
        return false;
    }
    const std::uint8_t mask = static_cast<std::uint8_t>(1U << channel);
    if (mask == currentMask_) {
        return true;
    }
    if (!writeMask(mask)) {
        configured_ = false;
        return false;
    }
    currentMask_ = mask;
    return true;
}

void Pca9548a::disable() {
    if (!configured_ || !wire_) {
        return;
    }
    writeMask(0);
    currentMask_ = 0;
}

bool Pca9548a::writeMask(std::uint8_t mask) {
    wire_->beginTransmission(address_);
    wire_->write(mask);
    return wire_->endTransmission() == 0;
}
}  // namespace TankRC::Drivers
