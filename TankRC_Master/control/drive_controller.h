#pragma once

#include "comms/radio_link.h"
#include "comms/slave_protocol.h"
#include "config/runtime_config.h"

#ifndef TANKRC_USE_DRIVE_PROXY
#if defined(TANKRC_BUILD_MASTER) && !defined(TANKRC_BUILD_SLAVE)
#define TANKRC_USE_DRIVE_PROXY 1
#else
#define TANKRC_USE_DRIVE_PROXY 0
#endif
#endif

#include "comms/slave_link.h"

#if defined(TANKRC_BUILD_MASTER) && !TANKRC_USE_DRIVE_PROXY
#error "Master firmware requires TANKRC_USE_DRIVE_PROXY=1"
#endif

namespace TankRC::Control {
class DriveController {
  public:
    void begin(const Config::RuntimeConfig& config);
    void setCommand(const Comms::DriveCommand& command);
    void setLightingCommand(const Comms::SlaveProtocol::LightingCommand& lighting);
    void update();
    float readBatteryVoltage();

  private:
    const Config::RuntimeConfig* config_ = nullptr;
    Comms::DriveCommand command_{};
    Comms::SlaveLink slave_;
};
}  // namespace TankRC::Control
