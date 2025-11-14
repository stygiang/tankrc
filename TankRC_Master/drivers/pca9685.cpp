#include <Arduino.h>
#include <Wire.h>
#include <algorithm>
#include <cmath>

#include "drivers/pca9685.h"

namespace TankRC::Drivers {
namespace {
constexpr std::uint8_t MODE1 = 0x00;
constexpr std::uint8_t MODE2 = 0x01;
constexpr std::uint8_t PRESCALE = 0xFE;
constexpr std::uint8_t LED0_ON_L = 0x06;
constexpr std::uint8_t ALL_LED_ON_L = 0xFA;
constexpr std::uint8_t RESTART = 0x80;
constexpr std::uint8_t SLEEP = 0x10;
constexpr std::uint8_t ALLCALL = 0x01;
constexpr std::uint8_t OUTDRV = 0x04;
}  // namespace

bool Pca9685::begin(std::uint8_t address, std::uint16_t frequency, TwoWire* wire) {
    wire_ = wire ? wire : &Wire;
    address_ = address;
    frequency_ = frequency;

    wire_->begin();

    write8(MODE1, 0x00);
    delay(5);
    write8(MODE2, OUTDRV);
    write8(MODE1, ALLCALL);
    delay(5);

    setFrequency(frequency_);
    ready_ = true;
    return true;
}

void Pca9685::setChannelValue(int channel, std::uint16_t value) {
    if (!ready_ || channel < 0 || channel > 15) {
        return;
    }
    if (value > 4095) {
        value = 4095;
    }
    setPwm(static_cast<std::uint8_t>(channel), 0, value);
}

void Pca9685::setChannelNormalized(int channel, float normalized) {
    normalized = std::clamp(normalized, 0.0F, 1.0F);
    const std::uint16_t value = static_cast<std::uint16_t>(normalized * 4095.0F + 0.5F);
    setChannelValue(channel, value);
}

void Pca9685::write8(std::uint8_t reg, std::uint8_t value) {
    wire_->beginTransmission(address_);
    wire_->write(reg);
    wire_->write(value);
    wire_->endTransmission();
}

void Pca9685::setPwm(std::uint8_t channel, std::uint16_t on, std::uint16_t off) {
    wire_->beginTransmission(address_);
    wire_->write(LED0_ON_L + 4 * channel);
    wire_->write(on & 0xFF);
    wire_->write(on >> 8);
    wire_->write(off & 0xFF);
    wire_->write(off >> 8);
    wire_->endTransmission();
}

void Pca9685::setFrequency(std::uint16_t freq) {
    if (freq < 24) {
        freq = 24;
    }
    if (freq > 1600) {
        freq = 1600;
    }
    const float oscClock = 25000000.0F;
    float prescaleval = (oscClock / (4096.0F * freq)) - 1.0F;
    const std::uint8_t prescale = static_cast<std::uint8_t>(floor(prescaleval + 0.5F));

    const std::uint8_t oldmode = [&]() {
        wire_->beginTransmission(address_);
        wire_->write(MODE1);
        wire_->endTransmission(false);
        wire_->requestFrom(address_, static_cast<std::uint8_t>(1));
        return wire_->read();
    }();

    const std::uint8_t sleepMode = (oldmode & ~RESTART) | SLEEP;
    write8(MODE1, sleepMode);
    write8(PRESCALE, prescale);
    write8(MODE1, oldmode);
    delay(5);
    write8(MODE1, oldmode | RESTART);
}
}  // namespace TankRC::Drivers
