#include <Arduino.h>
#include <algorithm>
#include <cstdint>

#include "features/lighting.h"

namespace TankRC::Features {
namespace {
constexpr float kTurnThreshold = 0.25F;
constexpr float kReverseThreshold = -0.15F;
constexpr Color kTurnColor{255, 140, 0};
constexpr Color kReverseColor{255, 255, 255};
constexpr Color kHeadlightActive{255, 255, 240};
constexpr Color kHeadlightDebug{0, 255, 255};
constexpr Color kHeadlightLocked{255, 32, 32};
constexpr Color kStatusDebug{0, 120, 255};
constexpr Color kStatusActive{255, 0, 0};
constexpr Color kStatusLocked{255, 0, 0};
constexpr Color kWifiColor{0, 180, 255};
constexpr Color kOff{0, 0, 0};

Color chooseModeColor(Comms::RcStatusMode mode) {
    switch (mode) {
        case Comms::RcStatusMode::Debug:
            return kHeadlightDebug;
        case Comms::RcStatusMode::Locked:
            return kHeadlightLocked;
        default:
            return kHeadlightActive;
    }
}

Color chooseStatusTailColor(Comms::RcStatusMode mode) {
    switch (mode) {
        case Comms::RcStatusMode::Debug:
            return kStatusDebug;
        case Comms::RcStatusMode::Locked:
            return kStatusLocked;
        default:
            return kStatusActive;
    }
}

Color makeColor(std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    return Color{r, g, b};
}
}  // namespace

void Lighting::begin(const Config::RuntimeConfig& config) {
    config_ = config.lighting;
    ready_ = pca_.begin(config_.pcaAddress, config_.pwmFrequency);
    if (ready_) {
        setAllLights(Color{0, 0, 0});
    }
}

void Lighting::setFeatureEnabled(bool enabled) {
    if (featureEnabled_ == enabled) {
        return;
    }
    featureEnabled_ = enabled;
    if (!featureEnabled_) {
        setAllLights(Color{0, 0, 0});
    }
}

void Lighting::update(const LightingInput& input) {
    if (!ready_) {
        return;
    }
    if (!featureEnabled_ && !input.hazard) {
        return;
    }

    const unsigned long now = millis();
    const std::uint16_t period = config_.blink.periodMs == 0 ? 500 : config_.blink.periodMs;
    if (now - lastBlinkToggleMs_ >= (period / 2)) {
        blinkState_ = !blinkState_;
        lastBlinkToggleMs_ = now;
    }

    if (applyHazardPattern(input)) {
        return;
    }

    if (applyConnectionPattern(input)) {
        return;
    }

    Color headlightBase = chooseModeColor(input.status);
    Color tailBase = chooseStatusTailColor(input.status);

    Color frontLeft = headlightBase;
    Color frontRight = headlightBase;
    Color rearLeft = tailBase;
    Color rearRight = tailBase;

    if (input.status == Comms::RcStatusMode::Locked) {
        frontLeft = frontRight = rearLeft = rearRight = kHeadlightLocked;
    }

    if (input.throttle < kReverseThreshold) {
        rearLeft = kReverseColor;
        rearRight = kReverseColor;
    }

    if (input.steering < -kTurnThreshold) {
        frontLeft = rearLeft = blinkState_ ? kTurnColor : kOff;
    } else if (input.steering > kTurnThreshold) {
        frontRight = rearRight = blinkState_ ? kTurnColor : kOff;
    }

    const float leftCloseness = std::clamp(1.0F - input.ultrasonicLeft, 0.0F, 1.0F);
    const float rightCloseness = std::clamp(1.0F - input.ultrasonicRight, 0.0F, 1.0F);
    if (leftCloseness > 0.01F) {
        const Color overlay = gradientFromSensor(input.ultrasonicLeft);
        frontLeft = blend(frontLeft, overlay, leftCloseness);
    }
    if (rightCloseness > 0.01F) {
        const Color overlay = gradientFromSensor(input.ultrasonicRight);
        frontRight = blend(frontRight, overlay, rightCloseness);
    }

    applyLight(config_.channels.frontLeft, frontLeft);
    applyLight(config_.channels.frontRight, frontRight);
    applyLight(config_.channels.rearLeft, rearLeft);
    applyLight(config_.channels.rearRight, rearRight);
}

void Lighting::setAllLights(const Color& color) {
    applyLight(config_.channels.frontLeft, color);
    applyLight(config_.channels.frontRight, color);
    applyLight(config_.channels.rearLeft, color);
    applyLight(config_.channels.rearRight, color);
}

void Lighting::applyLight(const Config::RgbChannel& channel, const Color& color) {
    if (channel.r < 0 || channel.g < 0 || channel.b < 0) {
        return;
    }
    const float rNorm = static_cast<float>(color.r) / 255.0F;
    const float gNorm = static_cast<float>(color.g) / 255.0F;
    const float bNorm = static_cast<float>(color.b) / 255.0F;
    pca_.setChannelNormalized(channel.r, rNorm);
    pca_.setChannelNormalized(channel.g, gNorm);
    pca_.setChannelNormalized(channel.b, bNorm);
}

bool Lighting::applyHazardPattern(const LightingInput& input) {
    if (!input.hazard) {
        hazardPhase_ = 0;
        hazardPhaseStartMs_ = millis();
        return false;
    }
    static constexpr unsigned long durations[] = {150, 150, 150, 450};
    const unsigned long now = millis();
    if (now - hazardPhaseStartMs_ >= durations[hazardPhase_]) {
        hazardPhase_ = (hazardPhase_ + 1) % 4;
        hazardPhaseStartMs_ = now;
    }
    const bool on = (hazardPhase_ == 0 || hazardPhase_ == 2);
    const Color color = on ? kTurnColor : kOff;
    applyLight(config_.channels.frontLeft, color);
    applyLight(config_.channels.frontRight, color);
    applyLight(config_.channels.rearLeft, color);
    applyLight(config_.channels.rearRight, color);
    return true;
}

bool Lighting::applyConnectionPattern(const LightingInput& input) {
    enum class Alert { None, Rc, Wifi };
    Alert alert = Alert::None;
    if (config_.blink.rc && !input.rcConnected) {
        alert = Alert::Rc;
    } else if (config_.blink.wifi && !input.wifiConnected) {
        alert = Alert::Wifi;
    }
    if (alert == Alert::None) {
        alertPhase_ = 0;
        alertPhaseStartMs_ = millis();
        return false;
    }

    const unsigned long now = millis();
    const unsigned long step = 180;
    if (now - alertPhaseStartMs_ >= step) {
        alertPhase_ = (alertPhase_ + 1) % 4;
        alertPhaseStartMs_ = now;
    }

    Color frontLeft = kOff;
    Color frontRight = kOff;
    Color rearLeft = kOff;
    Color rearRight = kOff;

    switch (alert) {
        case Alert::Rc: {
            const bool leftOn = (alertPhase_ % 2 == 0);
            const Color leftColor = leftOn ? kTurnColor : kOff;
            const Color rightColor = leftOn ? kOff : kTurnColor;
            frontLeft = rearLeft = leftColor;
            frontRight = rearRight = rightColor;
            break;
        }
        case Alert::Wifi: {
            const bool frontOn = (alertPhase_ < 2);
            const Color active = kWifiColor;
            frontLeft = frontRight = frontOn ? active : kOff;
            rearLeft = rearRight = frontOn ? kOff : active;
            break;
        }
        default:
            break;
    }

    applyLight(config_.channels.frontLeft, frontLeft);
    applyLight(config_.channels.frontRight, frontRight);
    applyLight(config_.channels.rearLeft, rearLeft);
    applyLight(config_.channels.rearRight, rearRight);
    return true;
}

Color Lighting::gradientFromSensor(float reading) const {
    float clamped = std::clamp(reading, 0.0F, 1.0F);
    float closeness = 1.0F - clamped;
    float r = 0.0F;
    float g = 0.0F;
    if (closeness <= 0.5F) {
        float t = closeness / 0.5F;  // 0..1
        r = 255.0F * t;
        g = 255.0F;
    } else {
        float t = (closeness - 0.5F) / 0.5F;
        r = 255.0F;
        g = 255.0F * (1.0F - t);
    }
    return Color{static_cast<std::uint8_t>(r), static_cast<std::uint8_t>(g), 0};
}

Color Lighting::blend(const Color& base, const Color& overlay, float mix) const {
    mix = std::clamp(mix, 0.0F, 1.0F);
    const float inv = 1.0F - mix;
    Color out{};
    out.r = static_cast<std::uint8_t>(std::clamp(base.r * inv + overlay.r * mix, 0.0F, 255.0F));
    out.g = static_cast<std::uint8_t>(std::clamp(base.g * inv + overlay.g * mix, 0.0F, 255.0F));
    out.b = static_cast<std::uint8_t>(std::clamp(base.b * inv + overlay.b * mix, 0.0F, 255.0F));
    return out;
}
}  // namespace TankRC::Features
