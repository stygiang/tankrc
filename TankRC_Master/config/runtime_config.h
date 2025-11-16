#pragma once
#ifndef TANKRC_CONFIG_RUNTIME_CONFIG_H
#define TANKRC_CONFIG_RUNTIME_CONFIG_H

#include <cstdint>

#include "config/features.h"

namespace TankRC::Config {
<<<<<<< HEAD
constexpr std::uint32_t kConfigVersion = 10;

enum class PinOwner : std::uint8_t { Slave = 0, IoExpander = 1 };

struct OwnedPin {
    int gpio = -1;
    std::uint8_t expanderPin = 0;
    PinOwner owner = PinOwner::Slave;
    std::uint8_t reserved = 0;
};
=======
constexpr std::uint32_t kConfigVersion = 6;
>>>>>>> parent of 45be129 (better help ui)

struct ChannelPins {
    int pwm = -1;
    OwnedPin in1{};
    OwnedPin in2{};
};

struct DriverPins {
    ChannelPins motorA{};
    ChannelPins motorB{};
    OwnedPin standby{};
};

struct IoExpanderConfig {
    bool enabled = false;
    bool useMux = false;
    std::uint8_t address = 0x20;
    std::uint8_t muxAddress = 0x70;
    std::uint8_t muxChannel = 0;
    std::uint8_t reserved = 0;
};

struct PinAssignments {
    DriverPins leftDriver{};
    DriverPins rightDriver{};
    int lightBar = -1;
    int speaker = -1;
    int batterySense = -1;
    int slaveTx = -1;
    int slaveRx = -1;
    IoExpanderConfig ioExpander{};
};

struct FeatureConfig {
    bool lightsEnabled = true;
    bool soundEnabled = true;
    bool sensorsEnabled = true;
    bool wifiEnabled = true;
    bool ultrasonicEnabled = true;
    bool tipOverEnabled = false;
};

struct RgbChannel {
    int r = -1;
    int g = -1;
    int b = -1;
};

struct LightingChannelMap {
    RgbChannel frontLeft{};
    RgbChannel frontRight{};
    RgbChannel rearLeft{};
    RgbChannel rearRight{};
};

struct LightingBlinkConfig {
    bool wifi = true;
    bool rc = true;
    bool bt = true;
    std::uint16_t periodMs = 500;
};

struct LightingConfig {
    std::uint8_t pcaAddress = 0x40;
    std::uint16_t pwmFrequency = 800;
    LightingChannelMap channels{};
    LightingBlinkConfig blink{};
};

struct WifiConfig {
    char ssid[32]{};
    char password[64]{};
    char apSsid[32]{};
    char apPassword[64]{};
};

struct NtpConfig {
    char server[48]{};
    std::int32_t gmtOffsetSeconds = 0;
    std::int32_t daylightOffsetSeconds = 0;
};

struct LoggingConfig {
    bool enabled = true;
    std::uint16_t maxEntries = 512;
};

struct RcConfig {
    int channelPins[6]{-1, -1, -1, -1, -1, -1};
};

struct RuntimeConfig {
    std::uint32_t version = kConfigVersion;
    PinAssignments pins{};
    FeatureConfig features{};
    LightingConfig lighting{};
    WifiConfig wifi{};
    NtpConfig ntp{};
    LoggingConfig logging{};
    RcConfig rc{};
};

RuntimeConfig makeDefaultConfig();
bool migrateConfig(RuntimeConfig& config, std::uint32_t fromVersion);
}  // namespace TankRC::Config
#endif  // TANKRC_CONFIG_RUNTIME_CONFIG_H
