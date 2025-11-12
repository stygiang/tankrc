#include <Arduino.h>

#include "drivers/rc_receiver.h"

namespace TankRC::Drivers {
namespace {
constexpr unsigned long kPulseMin = 1000UL;
constexpr unsigned long kPulseMax = 2000UL;
constexpr unsigned long kPulseMid = (kPulseMin + kPulseMax) / 2UL;
constexpr unsigned long kPulseRange = (kPulseMax - kPulseMin);

float normalizePulse(unsigned long width) {
    if (width == 0) {
        return 0.0F;
    }
    if (width < kPulseMin) {
        width = kPulseMin;
    } else if (width > kPulseMax) {
        width = kPulseMax;
    }
    const float centered = static_cast<float>(width) - static_cast<float>(kPulseMid);
    return centered / static_cast<float>(kPulseRange) * 2.0F;
}
}  // namespace

void RcReceiver::begin(const int* pins, std::size_t count) {
    for (std::size_t i = 0; i < kChannelCount; ++i) {
        pins_[i] = (i < count) ? pins[i] : -1;
        if (pins_[i] >= 0) {
            pinMode(pins_[i], INPUT);
        }
    }
    initialized_ = true;
}

RcReceiver::Frame RcReceiver::readFrame() {
    Frame frame{};
    frame.captureUs = micros();
    if (!initialized_) {
        return frame;
    }

    for (std::size_t i = 0; i < kChannelCount; ++i) {
        if (pins_[i] < 0) {
            continue;
        }
        const unsigned long width = pulseIn(pins_[i], HIGH, 25000UL);
        frame.widths[i] = width;
        frame.normalized[i] = normalizePulse(width);
    }
    return frame;
}
}  // namespace TankRC::Drivers
