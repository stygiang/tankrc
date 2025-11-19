#pragma once

#include <cstdint>
#include <Wire.h>

#include "comms/drive_types.h"
#include "config/runtime_config.h"
#include "drivers/pca9685.h"

namespace TankRC::Features {
struct Color {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
};

struct LightingInput {
    float steering = 0.0F;
    float throttle = 0.0F;
    bool rcConnected = true;
    bool wifiConnected = true;
    bool hazard = false;
    float ultrasonicLeft = 1.0F;
    float ultrasonicRight = 1.0F;
    Comms::RcStatusMode status = Comms::RcStatusMode::Active;
};

class Lighting {
  public:
    void begin(const Config::RuntimeConfig& config, TwoWire* bus = nullptr);
    void setFeatureEnabled(bool enabled);
    void update(const LightingInput& input);

  private:
    void setAllLights(const Color& color);
    void applyLight(const Config::RgbChannel& channel, const Color& color);

    bool applyHazardPattern(const LightingInput& input);
    bool applyConnectionPattern(const LightingInput& input);
    Color gradientFromSensor(float reading) const;
    Color blend(const Color& base, const Color& overlay, float mix) const;

    Drivers::Pca9685 pca_{};
    Config::LightingConfig config_{};
    bool ready_ = false;
    bool featureEnabled_ = true;
    bool blinkState_ = false;
    unsigned long lastBlinkToggleMs_ = 0UL;
    unsigned long hazardPhaseStartMs_ = 0UL;
    std::uint8_t hazardPhase_ = 0;
    unsigned long alertPhaseStartMs_ = 0UL;
    std::uint8_t alertPhase_ = 0;
};
}  // namespace TankRC::Features
