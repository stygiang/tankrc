#include <Arduino.h>
#include <cmath>

#include "drivers/motor_driver.h"

#include "drivers/io_expander.h"

namespace TankRC::Drivers {
namespace {
void configurePin(const DigitalPin& pin, IoExpander* expander) {
    switch (pin.source) {
        case PinSource::Gpio:
            if (pin.gpio >= 0) {
                pinMode(pin.gpio, OUTPUT);
            }
            break;
        case PinSource::IoExpander:
            if (expander && pin.valid()) {
                expander->pinMode(pin.expanderPin, true);
            }
            break;
        default:
            break;
    }
}
}  // namespace

void MotorDriver::attach(const ChannelPins& motorA,
                         const ChannelPins& motorB,
                         const DigitalPin& standbyPin,
                         IoExpander* expander) {
    motorA_ = motorA;
    motorB_ = motorB;
    standbyPin_ = standbyPin;
    expander_ = expander;

    if (motorA_.valid()) {
        pinMode(motorA_.pwm, OUTPUT);
        configurePin(motorA_.in1, expander_);
        configurePin(motorA_.in2, expander_);
    }

    if (motorB_.valid()) {
        pinMode(motorB_.pwm, OUTPUT);
        configurePin(motorB_.in1, expander_);
        configurePin(motorB_.in2, expander_);
    }

    if (standbyPin_.valid()) {
        configurePin(standbyPin_, expander_);
        writeDigital(standbyPin_, true);
    }

    stop();
}

void MotorDriver::setRampRate(float unitsPerSecond) {
    rampRate_ = unitsPerSecond <= 0.0F ? 1.0F : unitsPerSecond;
}

void MotorDriver::setTarget(float percent) {
    target_ = constrain(percent, -1.0F, 1.0F);
}

void MotorDriver::update(float dtSeconds) {
    if (dtSeconds <= 0.0F) {
        return;
    }

    const float delta = target_ - current_;
    const float step = rampRate_ * dtSeconds;

    if (fabsf(delta) <= step) {
        current_ = target_;
    } else {
        current_ += (delta > 0.0F ? step : -step);
    }

    driveChannel(motorA_, current_);
    driveChannel(motorB_, current_);
}

void MotorDriver::stop() {
    target_ = 0.0F;
    current_ = 0.0F;
    driveChannel(motorA_, 0.0F);
    driveChannel(motorB_, 0.0F);
}

void MotorDriver::driveChannel(const ChannelPins& pins, float percent) const {
    if (!pins.valid()) {
        return;
    }

    const float output = constrain(percent, -1.0F, 1.0F);
    const float magnitude = fabsf(output);

    if (magnitude <= 0.001F) {
        writeDigital(pins.in1, false);
        writeDigital(pins.in2, false);
        analogWrite(pins.pwm, 0);
        return;
    }

    const bool forward = output > 0.0F;
    writeDigital(pins.in1, forward);
    writeDigital(pins.in2, !forward);
    analogWrite(pins.pwm, static_cast<int>(magnitude * 255.0F));
}

void MotorDriver::writeDigital(const DigitalPin& pin, bool high) const {
    if (!pin.valid()) {
        return;
    }
    switch (pin.source) {
        case PinSource::Gpio:
            digitalWrite(pin.gpio, high ? HIGH : LOW);
            break;
        case PinSource::IoExpander:
            if (expander_) {
                expander_->digitalWrite(pin.expanderPin, high);
            }
            break;
        default:
            break;
    }
}
}  // namespace TankRC::Drivers
