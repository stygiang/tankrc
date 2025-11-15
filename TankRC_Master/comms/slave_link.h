#pragma once
#ifndef TANKRC_COMMS_SLAVE_LINK_H
#define TANKRC_COMMS_SLAVE_LINK_H

#include <array>
#include <cstdint>

#include <HardwareSerial.h>

#include "comms/radio_link.h"
#include "comms/slave_protocol.h"
#include "config/runtime_config.h"

namespace TankRC::Comms {
class SlaveLink {
  public:
    void begin(const Config::RuntimeConfig& config);
    void applyConfig(const Config::RuntimeConfig& config);
    void setCommand(const DriveCommand& command);
    void setLightingCommand(const SlaveProtocol::LightingCommand& lighting);
    void update();

    float batteryVoltage() const { return lastStatus_.batteryVoltage; }
    bool online() const;

  private:
    void sendFrame(SlaveProtocol::FrameType type, const std::uint8_t* payload, std::uint8_t length);
    void sendCommand();
    void processIncoming();
    void resetParser();

    enum class ParseState { Magic, Type, Length, Payload, Checksum };

    HardwareSerial* serial_ = &Serial1;
    int rxPin_ = 16;
    int txPin_ = 17;
    DriveCommand command_{};
    SlaveProtocol::LightingCommand lighting_{};
    bool commandDirty_ = false;
    unsigned long lastSendMs_ = 0;
    unsigned long lastStatusMs_ = 0;
    SlaveProtocol::StatusPayload lastStatus_{};

    ParseState parseState_ = ParseState::Magic;
    std::uint8_t currentType_ = 0;
    std::uint8_t expectedLength_ = 0;
    std::uint8_t payloadPos_ = 0;
    std::uint8_t checksum_ = 0;
    std::array<std::uint8_t, SlaveProtocol::kMaxPayload> payload_{};
};
}  // namespace TankRC::Comms
#endif  // TANKRC_COMMS_SLAVE_LINK_H
