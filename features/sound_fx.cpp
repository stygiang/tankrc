#include <Arduino.h>

#include "features/sound_fx.h"

namespace TankRC::Features {
void SoundFx::begin(int pin) {
    pin_ = pin;
    if (pin_ >= 0) {
        pinMode(pin_, OUTPUT);
    }
}

void SoundFx::setFeatureEnabled(bool enabled) {
    featureEnabled_ = enabled;
    if (!featureEnabled_) {
        update(false);
    }
}

void SoundFx::update(bool requestedState) {
    if (pin_ < 0) {
        return;
    }

    if (!featureEnabled_) {
        analogWrite(pin_, 0);
        return;
    }

    // Simple placeholder rumble tone.
    analogWrite(pin_, requestedState ? 128 : 0);
}
}  // namespace TankRC::Features
