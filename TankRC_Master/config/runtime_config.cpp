#include "config/runtime_config.h"

#include <algorithm>
#include <cstring>
#include <iterator>

#include "config/hardware_map.h"

namespace TankRC::Config {
RuntimeConfig makeDefaultConfig() {
    RuntimeConfig config{};

    config.pins.leftDriver.motorA = {Pins::LEFT_MOTOR1_PWM, Pins::LEFT_MOTOR1_IN1, Pins::LEFT_MOTOR1_IN2};
    config.pins.leftDriver.motorB = {Pins::LEFT_MOTOR2_PWM, Pins::LEFT_MOTOR2_IN1, Pins::LEFT_MOTOR2_IN2};
    config.pins.leftDriver.standby = Pins::LEFT_DRIVER_STBY;

    config.pins.rightDriver.motorA = {Pins::RIGHT_MOTOR1_PWM, Pins::RIGHT_MOTOR1_IN1, Pins::RIGHT_MOTOR1_IN2};
    config.pins.rightDriver.motorB = {Pins::RIGHT_MOTOR2_PWM, Pins::RIGHT_MOTOR2_IN1, Pins::RIGHT_MOTOR2_IN2};
    config.pins.rightDriver.standby = Pins::RIGHT_DRIVER_STBY;

    config.pins.lightBar = Pins::LIGHT_BAR;
    config.pins.speaker = Pins::SPEAKER;
    config.pins.batterySense = Pins::BATTERY_SENSE;
    config.pins.slaveTx = Pins::SLAVE_UART_TX;
    config.pins.slaveRx = Pins::SLAVE_UART_RX;
    config.pins.pcfAddress = 0x20;

    config.features.lightsEnabled = FEATURE_LIGHTS != 0;
    config.features.soundEnabled = FEATURE_SOUND != 0;
    config.features.sensorsEnabled = FEATURE_ULTRASONIC != 0;
    config.features.wifiEnabled = FEATURE_WIFI != 0;
    config.features.ultrasonicEnabled = FEATURE_ULTRASONIC != 0;
    config.features.tipOverEnabled = FEATURE_TIPOVER != 0;

    config.lighting.pcaAddress = 0x40;
    config.lighting.pwmFrequency = 800;
    config.lighting.channels.frontLeft = {0, 1, 2};
    config.lighting.channels.frontRight = {3, 4, 5};
    config.lighting.channels.rearLeft = {6, 7, 8};
    config.lighting.channels.rearRight = {9, 10, 11};
    config.lighting.blink.periodMs = 450;
    config.lighting.blink.wifi = true;
    config.lighting.blink.rc = true;

    std::strncpy(config.wifi.ssid, "", sizeof(config.wifi.ssid));
    std::strncpy(config.wifi.password, "", sizeof(config.wifi.password));
    std::strncpy(config.wifi.apSsid, "sharc", sizeof(config.wifi.apSsid));
    std::strncpy(config.wifi.apPassword, "tankrc123", sizeof(config.wifi.apPassword));
    config.wifi.ssid[sizeof(config.wifi.ssid) - 1] = '\0';
    config.wifi.password[sizeof(config.wifi.password) - 1] = '\0';
    config.wifi.apSsid[sizeof(config.wifi.apSsid) - 1] = '\0';
    config.wifi.apPassword[sizeof(config.wifi.apPassword) - 1] = '\0';

    std::strncpy(config.ntp.server, "pool.ntp.org", sizeof(config.ntp.server));
    config.ntp.server[sizeof(config.ntp.server) - 1] = '\0';
    config.ntp.gmtOffsetSeconds = 0;
    config.ntp.daylightOffsetSeconds = 0;

    config.logging.enabled = true;
    config.logging.maxEntries = 512;

    config.rc.channelPins[0] = Pins::RC_CH1;
    config.rc.channelPins[1] = Pins::RC_CH2;
    config.rc.channelPins[2] = Pins::RC_CH3;
    config.rc.channelPins[3] = Pins::RC_CH4;
    config.rc.channelPins[4] = Pins::RC_CH5;
    config.rc.channelPins[5] = Pins::RC_CH6;

    return config;
}

namespace {
constexpr int kMinGpio = -1;
constexpr int kMaxGpio = 39;
constexpr int kMinPcaChannel = -1;
constexpr int kMaxPcaChannel = 15;
constexpr int kMinPcfAddress = 0x20;
constexpr int kMaxPcfAddress = 0x27;

int clampGpio(int pin) {
    if (pin < kMinGpio || pin > kMaxGpio) {
        return -1;
    }
    return pin;
}

int clampPcaChannel(int value) {
    if (value < kMinPcaChannel || value > kMaxPcaChannel) {
        return -1;
    }
    return value;
}

bool normalizeGpio(int& pin, int defaultValue) {
    const int clamped = clampGpio(pin);
    bool changed = clamped != pin;
    pin = clamped;
    if (pin == -1 && defaultValue >= 0) {
        pin = defaultValue;
        changed = true;
    }
    return changed;
}

bool normalizeChannelPins(ChannelPins& pins, const ChannelPins& defaults) {
    bool changed = false;
    changed |= normalizeGpio(pins.pwm, defaults.pwm);
    changed |= normalizeGpio(pins.in1, defaults.in1);
    changed |= normalizeGpio(pins.in2, defaults.in2);
    return changed;
}

bool normalizeDriverPins(DriverPins& pins, const DriverPins& defaults) {
    bool changed = false;
    changed |= normalizeChannelPins(pins.motorA, defaults.motorA);
    changed |= normalizeChannelPins(pins.motorB, defaults.motorB);
    changed |= normalizeGpio(pins.standby, defaults.standby);
    return changed;
}

bool normalizeRgbChannel(RgbChannel& channel, const RgbChannel& defaults) {
    bool changed = false;
    auto normalize = [&](int& value, int def) {
        const int clamped = clampPcaChannel(value);
        if (clamped != value) {
            value = clamped;
            changed = true;
        }
        if (value == -1 && def >= 0) {
            value = def;
            changed = true;
        }
    };
    normalize(channel.r, defaults.r);
    normalize(channel.g, defaults.g);
    normalize(channel.b, defaults.b);
    return changed;
}

bool normalizeLightingChannels(LightingChannelMap& map, const LightingChannelMap& defaults) {
    bool changed = false;
    changed |= normalizeRgbChannel(map.frontLeft, defaults.frontLeft);
    changed |= normalizeRgbChannel(map.frontRight, defaults.frontRight);
    changed |= normalizeRgbChannel(map.rearLeft, defaults.rearLeft);
    changed |= normalizeRgbChannel(map.rearRight, defaults.rearRight);
    return changed;
}

template <typename T>
bool clampRange(T& value, T minValue, T maxValue) {
    const T clamped = std::clamp(value, minValue, maxValue);
    if (clamped != value) {
        value = clamped;
        return true;
    }
    return false;
}

void ensureStringTerminated(char* buffer, std::size_t length, bool& changed) {
    if (length == 0) {
        return;
    }
    if (buffer[length - 1] != '\0') {
        buffer[length - 1] = '\0';
        changed = true;
    }
}
}  // namespace

bool migrateConfig(RuntimeConfig& config, std::uint32_t fromVersion) {
    RuntimeConfig defaults = makeDefaultConfig();
    if (fromVersion == kConfigVersion) {
        return false;
    }

    bool changed = fromVersion != kConfigVersion;

    if (fromVersion == 0 || fromVersion > kConfigVersion) {
        config = defaults;
        config.version = kConfigVersion;
        return true;
    }

    changed |= normalizeDriverPins(config.pins.leftDriver, defaults.pins.leftDriver);
    changed |= normalizeDriverPins(config.pins.rightDriver, defaults.pins.rightDriver);
    changed |= normalizeGpio(config.pins.lightBar, defaults.pins.lightBar);
    changed |= normalizeGpio(config.pins.speaker, defaults.pins.speaker);
    changed |= normalizeGpio(config.pins.batterySense, defaults.pins.batterySense);
    changed |= normalizeGpio(config.pins.slaveTx, defaults.pins.slaveTx);
    changed |= normalizeGpio(config.pins.slaveRx, defaults.pins.slaveRx);
    if (config.pins.pcfAddress < kMinPcfAddress || config.pins.pcfAddress > kMaxPcfAddress) {
        config.pins.pcfAddress = defaults.pins.pcfAddress;
        changed = true;
    }

    for (std::size_t i = 0; i < std::size(config.rc.channelPins); ++i) {
        changed |= normalizeGpio(config.rc.channelPins[i], defaults.rc.channelPins[i]);
    }

    changed |= normalizeLightingChannels(config.lighting.channels, defaults.lighting.channels);
    changed |= clampRange<std::uint16_t>(config.lighting.pwmFrequency, 100, 1600);
    changed |= clampRange<std::uint16_t>(config.lighting.blink.periodMs, 100, 2000);
    changed |= clampRange<std::uint16_t>(config.logging.maxEntries, 32, defaults.logging.maxEntries);

    bool stringChanged = false;
    ensureStringTerminated(config.wifi.ssid, sizeof(config.wifi.ssid), stringChanged);
    ensureStringTerminated(config.wifi.password, sizeof(config.wifi.password), stringChanged);
    ensureStringTerminated(config.wifi.apSsid, sizeof(config.wifi.apSsid), stringChanged);
    ensureStringTerminated(config.wifi.apPassword, sizeof(config.wifi.apPassword), stringChanged);
    ensureStringTerminated(config.ntp.server, sizeof(config.ntp.server), stringChanged);
    if (stringChanged) {
        changed = true;
    }

    config.features.lightsEnabled = config.features.lightsEnabled && defaults.features.lightsEnabled;
    config.features.soundEnabled = config.features.soundEnabled && defaults.features.soundEnabled;
    config.features.sensorsEnabled = config.features.sensorsEnabled && defaults.features.sensorsEnabled;
    config.features.wifiEnabled = defaults.features.wifiEnabled;
    config.features.ultrasonicEnabled = defaults.features.ultrasonicEnabled;
    config.features.tipOverEnabled = defaults.features.tipOverEnabled;

    config.version = kConfigVersion;
    return changed;
}
}  // namespace TankRC::Config
