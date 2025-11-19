#include "hal/hal.h"

#include <Arduino.h>

#include "config/hardware_map.h"

namespace TankRC::Hal {
namespace {
Drivers::RcReceiver receiver;
int speakerPin = -1;
}  // namespace

void initializePlatform() {
    pinMode(Pins::STATUS_LED, OUTPUT);
    digitalWrite(Pins::STATUS_LED, LOW);
}

void begin(const Config::RuntimeConfig& config) {
    receiver.begin(config.rc.channelPins, Drivers::RcReceiver::kChannelCount);
    setSpeakerPin(config.pins.speaker);
}

void applyConfig(const Config::RuntimeConfig& config) {
    receiver.begin(config.rc.channelPins, Drivers::RcReceiver::kChannelCount);
    setSpeakerPin(config.pins.speaker);
}

std::uint32_t millis32() {
    return millis();
}

void delayMs(std::uint32_t ms) {
    delay(ms);
}

void toggleStatusLed() {
    digitalWrite(Pins::STATUS_LED, !digitalRead(Pins::STATUS_LED));
}

Drivers::RcReceiver::Frame readRcFrame() {
    return receiver.readFrame();
}

void setSpeakerPin(int pin) {
    speakerPin = pin;
    if (speakerPin >= 0) {
        pinMode(speakerPin, OUTPUT);
        analogWrite(speakerPin, 0);
    }
}

void writeSpeakerLevel(std::uint8_t duty) {
    if (speakerPin < 0) {
        return;
    }
    analogWrite(speakerPin, duty);
}
}  // namespace TankRC::Hal
