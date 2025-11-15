#include "hal/hal.h"

#include <Arduino.h>

#include "config/features.h"
#include "config/settings.h"
#include "drivers/battery_monitor.h"
#include "drivers/motor_driver.h"

namespace TankRC::Hal {
namespace {
Drivers::MotorDriver leftMotor;
Drivers::MotorDriver rightMotor;
Drivers::BatteryMonitor battery;
#if FEATURE_LIGHTS
Features::Lighting lighting;
#endif

Config::RuntimeConfig currentConfig{};
bool motorsReady = false;
#if FEATURE_LIGHTS
bool lightingReady = false;
#endif
}  // namespace

namespace {
Drivers::ChannelPins makeChannel(const Config::ChannelPins& pins) {
    return Drivers::ChannelPins{pins.pwm, pins.in1, pins.in2};
}

void configureMotors(const Config::RuntimeConfig& config) {
    const auto& pins = config.pins;
    leftMotor.attach(makeChannel(pins.leftDriver.motorA), makeChannel(pins.leftDriver.motorB), pins.leftDriver.standby);
    rightMotor.attach(makeChannel(pins.rightDriver.motorA), makeChannel(pins.rightDriver.motorB), pins.rightDriver.standby);
    const float ramp = Settings::motorDynamics.rampRate <= 0.0F ? 1.0F : Settings::motorDynamics.rampRate;
    leftMotor.setRampRate(ramp);
    rightMotor.setRampRate(ramp);
    motorsReady = true;
}

void configureBattery(const Config::RuntimeConfig& config) {
    if (config.pins.batterySense >= 0) {
        battery.attach(config.pins.batterySense, 2.0F);
    }
}

void configureLighting(const Config::RuntimeConfig& config) {
#if FEATURE_LIGHTS
    lighting.begin(config);
    lightingReady = true;
#else
    (void)config;
#endif
}
}  // namespace

void begin(const Config::RuntimeConfig& config) {
    currentConfig = config;
    motorsReady = false;
    configureMotors(config);
    configureBattery(config);
#if FEATURE_LIGHTS
    lightingReady = false;
    configureLighting(config);
#endif
}

void applyConfig(const Config::RuntimeConfig& config) {
    currentConfig = config;
    motorsReady = false;
    configureMotors(config);
    configureBattery(config);
#if FEATURE_LIGHTS
    lightingReady = false;
    configureLighting(config);
#endif
}

std::uint32_t millis32() {
    return millis();
}

void delayMs(std::uint32_t ms) {
    delay(ms);
}

void setMotorOutputs(float left, float right) {
    if (!motorsReady) {
        return;
    }
    leftMotor.setTarget(left);
    rightMotor.setTarget(right);
}

void updateMotorController(float dtSeconds) {
    if (!motorsReady) {
        return;
    }
    leftMotor.update(dtSeconds);
    rightMotor.update(dtSeconds);
}

void stopMotors() {
    if (!motorsReady) {
        return;
    }
    leftMotor.stop();
    rightMotor.stop();
}

float readBatteryVoltage() {
    return battery.readVoltage();
}

void setLightingEnabled(bool enabled) {
#if FEATURE_LIGHTS
    if (!lightingReady) {
        return;
    }
    lighting.setFeatureEnabled(enabled);
#else
    (void)enabled;
#endif
}

void updateLighting(const Features::LightingInput& input) {
#if FEATURE_LIGHTS
    if (!lightingReady) {
        return;
    }
    lighting.update(input);
#else
    (void)input;
#endif
}
}  // namespace TankRC::Hal
