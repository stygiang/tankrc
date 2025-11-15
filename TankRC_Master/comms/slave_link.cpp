#include "comms/slave_link.h"

#include <Arduino.h>
#include <cstring>

namespace TankRC::Comms {
namespace {
constexpr unsigned long kCommandIntervalMs = 20;
constexpr unsigned long kStatusTimeoutMs = 500;
}  // namespace

void SlaveLink::begin(const Config::RuntimeConfig& config) {
    rxPin_ = config.pins.slaveRx;
    txPin_ = config.pins.slaveTx;
    if (rxPin_ >= 0 && txPin_ >= 0) {
        serial_->begin(921600, SERIAL_8N1, rxPin_, txPin_);
    } else {
        serial_->begin(921600);
    }
    resetParser();
    applyConfig(config);
}

void SlaveLink::applyConfig(const Config::RuntimeConfig& config) {
    SlaveProtocol::ConfigPayload payload{};
    payload.pins = config.pins;
    sendFrame(SlaveProtocol::FrameType::Config,
              reinterpret_cast<const std::uint8_t*>(&payload),
              sizeof(payload));
}

void SlaveLink::setCommand(const DriveCommand& command) {
    command_ = command;
    commandDirty_ = true;
}

void SlaveLink::update() {
    processIncoming();
    const unsigned long now = millis();
    if (commandDirty_ || (now - lastSendMs_) >= kCommandIntervalMs) {
        sendCommand();
        commandDirty_ = false;
        lastSendMs_ = now;
    }
}

bool SlaveLink::online() const {
    return (lastStatusMs_ != 0) && (millis() - lastStatusMs_ < kStatusTimeoutMs);
}

void SlaveLink::sendCommand() {
    SlaveProtocol::CommandPayload payload{};
    payload.throttle = command_.throttle;
    payload.turn = command_.turn;
    sendFrame(SlaveProtocol::FrameType::Command,
              reinterpret_cast<const std::uint8_t*>(&payload),
              sizeof(payload));
}

void SlaveLink::sendFrame(SlaveProtocol::FrameType type, const std::uint8_t* payload, std::uint8_t length) {
    if (!serial_) {
        return;
    }
    serial_->write(SlaveProtocol::kMagic);
    serial_->write(static_cast<std::uint8_t>(type));
    serial_->write(length);
    if (payload && length > 0) {
        serial_->write(payload, length);
    }
    const std::uint8_t sum = SlaveProtocol::checksum(type, length, payload);
    serial_->write(sum);
}

void SlaveLink::processIncoming() {
    if (!serial_) {
        return;
    }

    while (serial_->available()) {
        std::uint8_t byte = static_cast<std::uint8_t>(serial_->read());
        switch (parseState_) {
            case ParseState::Magic:
                if (byte == SlaveProtocol::kMagic) {
                    parseState_ = ParseState::Type;
                }
                break;
            case ParseState::Type:
                currentType_ = byte;
                parseState_ = ParseState::Length;
                break;
            case ParseState::Length:
                expectedLength_ = byte;
                payloadPos_ = 0;
                checksum_ = static_cast<std::uint8_t>(currentType_) ^ expectedLength_;
                if (expectedLength_ > payload_.size()) {
                    resetParser();
                } else if (expectedLength_ == 0) {
                    parseState_ = ParseState::Checksum;
                } else {
                    parseState_ = ParseState::Payload;
                }
                break;
            case ParseState::Payload:
                payload_[payloadPos_++] = byte;
                checksum_ ^= byte;
                if (payloadPos_ >= expectedLength_) {
                    parseState_ = ParseState::Checksum;
                }
                break;
            case ParseState::Checksum:
                if (checksum_ == byte) {
                    if (currentType_ == static_cast<std::uint8_t>(SlaveProtocol::FrameType::Status) &&
                        expectedLength_ == sizeof(SlaveProtocol::StatusPayload)) {
                        std::memcpy(&lastStatus_, payload_.data(), sizeof(SlaveProtocol::StatusPayload));
                        lastStatusMs_ = millis();
                    }
                }
                resetParser();
                break;
        }
    }
}

void SlaveLink::resetParser() {
    parseState_ = ParseState::Magic;
    currentType_ = 0;
    expectedLength_ = 0;
    payloadPos_ = 0;
    checksum_ = 0;
}
}  // namespace TankRC::Comms
