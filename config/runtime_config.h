#pragma once
#ifndef TANKRC_CONFIG_RUNTIME_CONFIG_H
#define TANKRC_CONFIG_RUNTIME_CONFIG_H

#include <cstdint>

namespace TankRC::Config {
constexpr std::uint32_t kConfigVersion = 1;

struct ChannelPins {
    int pwm = -1;
    int in1 = -1;
    int in2 = -1;
};

struct DriverPins {
    ChannelPins motorA{};
    ChannelPins motorB{};
    int standby = -1;
};

struct PinAssignments {
    DriverPins leftDriver{};
    DriverPins rightDriver{};
    int lightBar = -1;
    int speaker = -1;
    int batterySense = -1;
};

struct FeatureConfig {
    bool lightingEnabled = true;
    bool soundEnabled = true;
    bool sensorsEnabled = true;
};

struct RuntimeConfig {
    std::uint32_t version = kConfigVersion;
    PinAssignments pins{};
    FeatureConfig features{};
};

RuntimeConfig makeDefaultConfig();
}  // namespace TankRC::Config
#endif  // TANKRC_CONFIG_RUNTIME_CONFIG_H
