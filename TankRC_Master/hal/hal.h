#pragma once

#include <cstdint>

#include "config/runtime_config.h"
#include "drivers/rc_receiver.h"

namespace TankRC::Hal {
void initializePlatform();
void begin(const Config::RuntimeConfig& config);
void applyConfig(const Config::RuntimeConfig& config);

std::uint32_t millis32();
void delayMs(std::uint32_t ms);

void toggleStatusLed();

Drivers::RcReceiver::Frame readRcFrame();

void setSpeakerPin(int pin);
void writeSpeakerLevel(std::uint8_t duty);
}  // namespace TankRC::Hal
