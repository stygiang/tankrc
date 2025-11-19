#include "drivers/pcf8575.h"

namespace TankRC::Drivers {
bool Pcf8575::begin(std::uint8_t address, TwoWire* wire) {
    address_ = address;
    if (wire) {
        wire_ = wire;
    }
    state_ = 0xFFFF;
    if (wire == nullptr) {
        wire_->begin();
    }
    ready_ = flush();
    return ready_;
}

void Pcf8575::writePin(int index, bool high) {
    if (!ready_ || index < 0 || index >= 16) {
        return;
    }
    const std::uint16_t mask = static_cast<std::uint16_t>(1u << index);
    if (high) {
        state_ |= mask;
    } else {
        state_ &= static_cast<std::uint16_t>(~mask);
    }
    flush();
}

bool Pcf8575::flush() {
    wire_->beginTransmission(address_);
    wire_->write(state_ & 0xFF);
    wire_->write((state_ >> 8) & 0xFF);
    ready_ = wire_->endTransmission() == 0;
    return ready_;
}
}  // namespace TankRC::Drivers
