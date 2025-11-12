#include <Arduino.h>
#include <algorithm>

#include "comms/radio_link.h"

namespace TankRC::Comms {
namespace {
float clampRange(float value) {
    return std::max(-1.0F, std::min(1.0F, value));
}

RcStatusMode modeFromChannel(float value) {
    if (value > 0.33F) {
        return RcStatusMode::Debug;
    }
    if (value < -0.33F) {
        return RcStatusMode::Locked;
    }
    return RcStatusMode::Active;
}

float toZeroOne(float value) {
    return (clampRange(value) + 1.0F) * 0.5F;
}
}  // namespace

void RadioLink::begin(const Config::RuntimeConfig& config) {
    rcConfig_ = config.rc;
    receiver_.begin(rcConfig_.channelPins, Drivers::RcReceiver::kChannelCount);
}

CommandPacket RadioLink::poll() {
    CommandPacket packet{};
    const auto frame = receiver_.readFrame();

    packet.drive.turn = clampRange(frame.normalized[0]);
    packet.drive.throttle = clampRange(frame.normalized[1]);
    packet.auxButton = frame.normalized[2] > 0.25F;
    packet.status = modeFromChannel(frame.normalized[3]);
    packet.auxChannel5 = toZeroOne(frame.normalized[4]);
    packet.auxChannel6 = toZeroOne(frame.normalized[5]);

    // Simple defaults: aux button toggles lighting, sound follows mode.
    packet.lightingState = packet.auxButton;
    packet.soundState = packet.status == RcStatusMode::Active;

    return packet;
}
}  // namespace TankRC::Comms
