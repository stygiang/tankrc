#pragma once

namespace TankRC::Comms {
struct DriveCommand {
    float throttle = 0.0F;
    float turn = 0.0F;
};

enum class RcStatusMode { Debug, Active, Locked };
}  // namespace TankRC::Comms
