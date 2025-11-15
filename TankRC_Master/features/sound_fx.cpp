#include "features/sound_fx.h"
#include "hal/hal.h"

namespace TankRC::Features {
void SoundFx::begin(int pin) {
    pin_ = pin;
    Hal::setSpeakerPin(pin_);
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

    const bool active = featureEnabled_ && requestedState;
    Hal::writeSpeakerLevel(active ? 128 : 0);
}
}  // namespace TankRC::Features
