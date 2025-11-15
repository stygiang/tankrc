#pragma once
#ifndef TANKRC_COMMS_RADIO_LINK_H
#define TANKRC_COMMS_RADIO_LINK_H

#include "config/runtime_config.h"
#include "drivers/rc_receiver.h"

namespace TankRC::Comms {
struct DriveCommand {
    float throttle = 0.0F;
    float turn = 0.0F;
};

enum class RcStatusMode { Debug, Active, Locked };

struct CommandPacket {
    DriveCommand drive{};
    bool lightingState = false;
    bool soundState = false;
    bool auxButton = false;
    bool hazard = false;
    RcStatusMode status = RcStatusMode::Active;
    float auxChannel5 = 0.0F;
    float auxChannel6 = 0.0F;
    bool rcLinked = true;
    bool wifiConnected = true;
};

class RadioLink {
  public:
    void begin(const Config::RuntimeConfig& config);
    CommandPacket poll();

  private:
    Drivers::RcReceiver receiver_{};
    Config::RcConfig rcConfig_{};
};
}  // namespace TankRC::Comms
#endif  // TANKRC_COMMS_RADIO_LINK_H
