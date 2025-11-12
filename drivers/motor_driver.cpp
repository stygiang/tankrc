#include <Arduino.h>
#include <cmath>

#include "drivers/motor_driver.h"

namespace TankRC::Drivers {
void MotorDriver::attach(const ChannelPins& motorA, const ChannelPins& motorB, int standbyPin) {
    motorA_ = motorA;
    motorB_ = motorB;
    standbyPin_ = standbyPin;

    if (motorA_.valid()) {
        pinMode(motorA_.pwm, OUTPUT);
        pinMode(motorA_.in1, OUTPUT);
        pinMode(motorA_.in2, OUTPUT);
    }

    if (motorB_.valid()) {
        pinMode(motorB_.pwm, OUTPUT);
        pinMode(motorB_.in1, OUTPUT);
        pinMode(motorB_.in2, OUTPUT);
    }

    if (standbyPin_ >= 0) {
        pinMode(standbyPin_, OUTPUT);
        digitalWrite(standbyPin_, HIGH);
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
        digitalWrite(pins.in1, LOW);
        digitalWrite(pins.in2, LOW);
        analogWrite(pins.pwm, 0);
        return;
    }

    const bool forward = output > 0.0F;
    digitalWrite(pins.in1, forward ? HIGH : LOW);
    digitalWrite(pins.in2, forward ? LOW : HIGH);
    analogWrite(pins.pwm, static_cast<int>(magnitude * 255.0F));
}
}  // namespace TankRC::Drivers
