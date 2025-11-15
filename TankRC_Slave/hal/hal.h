#pragma once

#include <cstdint>

#include "config/runtime_config.h"
#include "features/lighting.h"

namespace TankRC::Hal {
void begin(const Config::RuntimeConfig& config);
void applyConfig(const Config::RuntimeConfig& config);

std::uint32_t millis32();
void delayMs(std::uint32_t ms);

void setMotorOutputs(float left, float right);
void updateMotorController(float dtSeconds);
void stopMotors();

float readBatteryVoltage();

void setLightingEnabled(bool enabled);
void updateLighting(const Features::LightingInput& input);
}  // namespace TankRC::Hal
