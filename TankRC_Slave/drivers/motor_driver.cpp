#include <Arduino.h>
#include <cmath>

#include "drivers/motor_driver.h"

namespace TankRC::Drivers {
void MotorDriver::attach(const ChannelPins& motorA, const ChannelPins& motorB, int standbyPin, Pcf8575* expander) {
    motorA_ = motorA;
    motorB_ = motorB;
    standbyPin_ = standbyPin;
    expander_ = expander;

    auto setupPin = [](int pin) {
        if (pin >= 0) {
            pinMode(pin, OUTPUT);
        }
    };

    if (motorA_.valid()) {
        setupPin(motorA_.pwm);
        setupPin(motorA_.in1);
        setupPin(motorA_.in2);
    }

    if (motorB_.valid()) {
        setupPin(motorB_.pwm);
        setupPin(motorB_.in1);
        setupPin(motorB_.in2);
    }

    if (standbyPin_ >= 0) {
        pinMode(standbyPin_, OUTPUT);
        digitalWrite(standbyPin_, HIGH);
    } else if (Config::isPcfPin(standbyPin_)) {
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
        if (pins.pwm >= 0) {
            analogWrite(pins.pwm, 0);
        }
        return;
    }

    const bool forward = output > 0.0F;
    writeDigital(pins.in1, forward);
    writeDigital(pins.in2, !forward);
    if (pins.pwm >= 0) {
        analogWrite(pins.pwm, static_cast<int>(magnitude * 255.0F));
    }
}

void MotorDriver::writeDigital(int pin, bool high) const {
    if (pin < 0) {
        if (Config::isPcfPin(pin) && expander_) {
            const int idx = Config::pcfIndexFromPin(pin);
            expander_->writePin(idx, high);
        }
        return;
    }
    digitalWrite(pin, high ? HIGH : LOW);
}
}  // namespace TankRC::Drivers
