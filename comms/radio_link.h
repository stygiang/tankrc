#pragma once
#ifndef TANKRC_COMMS_RADIO_LINK_H
#define TANKRC_COMMS_RADIO_LINK_H

namespace TankRC::Comms {
struct DriveCommand {
    float throttle = 0.0F;
    float turn = 0.0F;
};

struct CommandPacket {
    DriveCommand drive{};
    bool lightingState = false;
    bool soundState = false;
};

class RadioLink {
  public:
    void begin();
    CommandPacket poll();
};
}  // namespace TankRC::Comms
#endif  // TANKRC_COMMS_RADIO_LINK_H
