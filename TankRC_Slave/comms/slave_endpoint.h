#pragma once
#ifndef TANKRC_COMMS_SLAVE_ENDPOINT_H
#define TANKRC_COMMS_SLAVE_ENDPOINT_H

#include <array>

#include <HardwareSerial.h>

#include "comms/drive_types.h"
#include "comms/slave_protocol.h"
#include "config/runtime_config.h"
#include "features/lighting.h"

namespace TankRC::Control {
class DriveController;
}

namespace TankRC::Comms {
class SlaveEndpoint {
  public:
    void begin(Config::RuntimeConfig* config,
               Control::DriveController* drive,
               Features::Lighting* lighting,
               HardwareSerial* serial = &Serial1);
    void loop();

  private:
    enum class ParseState { Magic, Type, Length, Payload, Checksum };

    void processByte(std::uint8_t byte);
    void processFrame(std::uint8_t type, std::uint8_t length);
    void handleConfig(const SlaveProtocol::ConfigPayload& payload);
    void handleCommand(const SlaveProtocol::CommandPayload& payload);
    void sendStatus();
    void resetParser();

    Config::RuntimeConfig* config_ = nullptr;
    Control::DriveController* drive_ = nullptr;
    Features::Lighting* lighting_ = nullptr;
    HardwareSerial* serial_ = nullptr;
    ParseState state_ = ParseState::Magic;
    std::uint8_t currentType_ = 0;
    std::uint8_t expectedLength_ = 0;
    std::uint8_t payloadPos_ = 0;
    std::uint8_t checksum_ = 0;
    std::array<std::uint8_t, SlaveProtocol::kMaxPayload> payload_{};
    Comms::DriveCommand currentCommand_{};
    Features::LightingInput lightingInput_{};
    bool lightingEnabled_ = false;
    unsigned long lastCommandMs_ = 0;
    unsigned long lastStatusMs_ = 0;
    int rxPin_ = -1;
    int txPin_ = -1;
};
}  // namespace TankRC::Comms
#endif  // TANKRC_COMMS_SLAVE_ENDPOINT_H
