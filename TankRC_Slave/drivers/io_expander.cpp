#include "drivers/io_expander.h"

#include <Wire.h>

namespace TankRC::Drivers {
namespace {
constexpr std::uint8_t kRegInput0 = 0x00;
constexpr std::uint8_t kRegOutput0 = 0x02;
constexpr std::uint8_t kRegPolarity0 = 0x04;
constexpr std::uint8_t kRegConfig0 = 0x06;

std::uint8_t lowByte(std::uint16_t value) {
    return static_cast<std::uint8_t>(value & 0xFF);
}

std::uint8_t highByte(std::uint16_t value) {
    return static_cast<std::uint8_t>((value >> 8) & 0xFF);
}
}

void IoExpander::configure(std::uint8_t address,
                           bool useMux,
                           std::uint8_t muxAddress,
                           std::uint8_t muxChannel) {
    address_ = address;
    useMux_ = useMux;
    muxAddress_ = muxAddress;
    muxChannel_ = muxChannel;
}

bool IoExpander::begin(TwoWire* wire) {
    wire_ = wire ? wire : &Wire;
    if (!wire_) {
        ready_ = false;
        return false;
    }
    if (useMux_) {
        mux_.configure(muxAddress_, wire_);
        if (!mux_.selectChannel(muxChannel_)) {
            ready_ = false;
            return false;
        }
    }
    direction_ = 0xFFFF;
    outputs_ = 0x0000;
    directionDirty_ = true;
    outputsDirty_ = true;
    ready_ = updateDirection() && updateOutputs();
    return ready_;
}

void IoExpander::pinMode(std::uint8_t pin, bool output) {
    if (!ready_ || pin >= 16) {
        return;
    }
    const std::uint16_t mask = static_cast<std::uint16_t>(1U << pin);
    const std::uint16_t previous = direction_;
    if (output) {
        direction_ &= ~mask;
    } else {
        direction_ |= mask;
    }
    if (direction_ != previous) {
        directionDirty_ = true;
        updateDirection();
    }
}

void IoExpander::digitalWrite(std::uint8_t pin, bool high) {
    if (!ready_ || pin >= 16) {
        return;
    }
    const std::uint16_t mask = static_cast<std::uint16_t>(1U << pin);
    const std::uint16_t previous = outputs_;
    if (high) {
        outputs_ |= mask;
    } else {
        outputs_ &= ~mask;
    }
    if (outputs_ != previous) {
        outputsDirty_ = true;
        updateOutputs();
    }
}

bool IoExpander::ensureBusSelected() const {
    if (!useMux_) {
        return true;
    }
    return mux_.selectChannel(muxChannel_);
}

bool IoExpander::writeRegister(std::uint8_t reg, std::uint16_t value) const {
    if (!ensureBusSelected()) {
        return false;
    }
    wire_->beginTransmission(address_);
    wire_->write(reg);
    wire_->write(lowByte(value));
    wire_->write(highByte(value));
    return wire_->endTransmission() == 0;
}

bool IoExpander::readRegister(std::uint8_t reg, std::uint16_t& value) const {
    if (!ensureBusSelected()) {
        return false;
    }
    wire_->beginTransmission(address_);
    wire_->write(reg);
    if (wire_->endTransmission(false) != 0) {
        return false;
    }
    if (wire_->requestFrom(address_, static_cast<std::uint8_t>(2)) != 2) {
        return false;
    }
    const std::uint8_t low = wire_->read();
    const std::uint8_t high = wire_->read();
    value = static_cast<std::uint16_t>(low | (static_cast<std::uint16_t>(high) << 8));
    return true;
}

bool IoExpander::updateOutputs() const {
    if (!ready_) {
        return false;
    }
    if (!outputsDirty_) {
        return true;
    }
    const bool ok = writeRegister(kRegOutput0, outputs_);
    if (ok) {
        outputsDirty_ = false;
    }
    return ok;
}

bool IoExpander::updateDirection() const {
    if (!ready_) {
        return false;
    }
    if (!directionDirty_) {
        // Always write direction during init to ensure known state.
        directionDirty_ = true;
    }
    const bool ok = writeRegister(kRegConfig0, direction_);
    if (ok) {
        directionDirty_ = false;
    }
    return ok;
}
}  // namespace TankRC::Drivers
