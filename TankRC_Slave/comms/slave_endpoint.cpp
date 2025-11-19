#include "comms/slave_endpoint.h"

#include <Arduino.h>
#include <cstring>

#include "config/pins.h"
#include "control/drive_controller.h"

namespace TankRC::Comms {
namespace {
constexpr unsigned long kCommandTimeoutMs = 500;
constexpr unsigned long kStatusIntervalMs = 100;
}  // namespace

void SlaveEndpoint::begin(Config::RuntimeConfig* config,
                          Control::DriveController* drive,
                          HardwareSerial* serial) {
    config_ = config;
    drive_ = drive;
    serial_ = serial ? serial : &Serial1;
    if (serial_) {
        rxPin_ = Pins::SLAVE_UART_RX;
        txPin_ = Pins::SLAVE_UART_TX;
        if (rxPin_ >= 0 && txPin_ >= 0) {
            serial_->begin(921600, SERIAL_8N1, rxPin_, txPin_);
        } else {
            serial_->begin(921600);
        }
    }
    if (config_ && drive_) {
        drive_->begin(*config_);
    }
    lightingEnabled_ = config_ ? config_->features.lightsEnabled : false;
    resetParser();
}

void SlaveEndpoint::loop() {
    if (!serial_ || !drive_) {
        return;
    }

    while (serial_->available()) {
        std::uint8_t byte = static_cast<std::uint8_t>(serial_->read());
        processByte(byte);
    }

    const unsigned long now = Hal::millis32();
    if ((now - lastCommandMs_) > kCommandTimeoutMs) {
        currentCommand_ = {};
        lightingInput_ = {};
        lightingEnabled_ = false;
    }

    drive_->setCommand(currentCommand_);
    drive_->update();
    Hal::setLightingEnabled(lightingEnabled_);
    Hal::updateLighting(lightingInput_);

    if ((now - lastStatusMs_) >= kStatusIntervalMs) {
        sendStatus();
        lastStatusMs_ = now;
    }
}

void SlaveEndpoint::processByte(std::uint8_t byte) {
    switch (state_) {
        case ParseState::Magic:
            if (byte == SlaveProtocol::kMagic) {
                state_ = ParseState::Type;
            }
            break;
        case ParseState::Type:
            currentType_ = byte;
            state_ = ParseState::Length;
            break;
        case ParseState::Length:
            expectedLength_ = byte;
            payloadPos_ = 0;
            checksum_ = static_cast<std::uint8_t>(currentType_) ^ expectedLength_;
            if (expectedLength_ > payload_.size()) {
                resetParser();
            } else if (expectedLength_ == 0) {
                state_ = ParseState::Checksum;
            } else {
                state_ = ParseState::Payload;
            }
            break;
        case ParseState::Payload:
            payload_[payloadPos_++] = byte;
            checksum_ ^= byte;
            if (payloadPos_ >= expectedLength_) {
                state_ = ParseState::Checksum;
            }
            break;
        case ParseState::Checksum:
            if (checksum_ == byte) {
                processFrame(currentType_, expectedLength_);
            }
            resetParser();
            break;
    }
}

void SlaveEndpoint::processFrame(std::uint8_t type, std::uint8_t length) {
    if (type == static_cast<std::uint8_t>(SlaveProtocol::FrameType::Config) &&
        length == sizeof(SlaveProtocol::ConfigPayload)) {
        SlaveProtocol::ConfigPayload payload{};
        std::memcpy(&payload, payload_.data(), sizeof(payload));
        handleConfig(payload);
        return;
    }

    if (type == static_cast<std::uint8_t>(SlaveProtocol::FrameType::Command) &&
        length == sizeof(SlaveProtocol::CommandPayload)) {
        SlaveProtocol::CommandPayload payload{};
        std::memcpy(&payload, payload_.data(), sizeof(payload));
        handleCommand(payload);
    }
}

void SlaveEndpoint::handleConfig(const SlaveProtocol::ConfigPayload& payload) {
    if (!config_) {
        return;
    }
    config_->pins = payload.pins;
    config_->features = payload.features;
    config_->lighting = payload.lighting;
    // The slave board uses its own UART pins; overwrite any host-provided values.
    config_->pins.slaveRx = Pins::SLAVE_UART_RX;
    config_->pins.slaveTx = Pins::SLAVE_UART_TX;
    if (serial_) {
        const bool pinsChanged = (rxPin_ != Pins::SLAVE_UART_RX) || (txPin_ != Pins::SLAVE_UART_TX);
        if (pinsChanged) {
            serial_->end();
            rxPin_ = Pins::SLAVE_UART_RX;
            txPin_ = Pins::SLAVE_UART_TX;
            if (rxPin_ >= 0 && txPin_ >= 0) {
                serial_->begin(921600, SERIAL_8N1, rxPin_, txPin_);
            } else {
                serial_->begin(921600);
            }
        }
    }
    Hal::applyConfig(*config_);
    lightingEnabled_ = config_->features.lightsEnabled;
    if (drive_) {
        drive_->begin(*config_);
    }
}

void SlaveEndpoint::handleCommand(const SlaveProtocol::CommandPayload& payload) {
    currentCommand_.throttle = payload.throttle;
    currentCommand_.turn = payload.turn;
    lightingInput_.steering = payload.turn;
    lightingInput_.throttle = payload.throttle;
    lightingInput_.ultrasonicLeft = payload.lighting.ultrasonicLeft;
    lightingInput_.ultrasonicRight = payload.lighting.ultrasonicRight;
    lightingInput_.status = static_cast<Comms::RcStatusMode>(payload.lighting.status);
    lightingInput_.hazard = (payload.lighting.flags & SlaveProtocol::LightingHazard) != 0;
    lightingInput_.rcConnected = (payload.lighting.flags & SlaveProtocol::LightingRcLinked) != 0;
    lightingInput_.wifiConnected = (payload.lighting.flags & SlaveProtocol::LightingWifiLinked) != 0;
    lightingEnabled_ = (payload.lighting.flags & SlaveProtocol::LightingEnabled) != 0;
    lastCommandMs_ = Hal::millis32();
}

void SlaveEndpoint::sendStatus() {
    if (!serial_ || !drive_) {
        return;
    }
    SlaveProtocol::StatusPayload status{};
    status.batteryVoltage = drive_->readBatteryVoltage();
    const auto type = SlaveProtocol::FrameType::Status;
    serial_->write(SlaveProtocol::kMagic);
    serial_->write(static_cast<std::uint8_t>(type));
    const std::uint8_t length = sizeof(status);
    serial_->write(length);
    const std::uint8_t* payload = reinterpret_cast<const std::uint8_t*>(&status);
    serial_->write(payload, length);
    const std::uint8_t sum = SlaveProtocol::checksum(type, length, payload);
    serial_->write(sum);
}

void SlaveEndpoint::resetParser() {
    state_ = ParseState::Magic;
    currentType_ = 0;
    expectedLength_ = 0;
    payloadPos_ = 0;
    checksum_ = 0;
}
}  // namespace TankRC::Comms
