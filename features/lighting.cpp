#include <Arduino.h>

#include "features/lighting.h"

namespace TankRC::Features {
void Lighting::begin(int pin) {
    pin_ = pin;
    if (pin_ >= 0) {
        pinMode(pin_, OUTPUT);
    }
}

void Lighting::setFeatureEnabled(bool enabled) {
    featureEnabled_ = enabled;
    if (!featureEnabled_) {
        update(false);
    }
}

void Lighting::update(bool requestedState) {
    if (pin_ < 0 || !featureEnabled_) {
        return;
    }

    analogWrite(pin_, requestedState ? 255 : 0);
}
}  // namespace TankRC::Features
