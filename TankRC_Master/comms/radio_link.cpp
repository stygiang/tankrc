#include <Arduino.h>
#include <algorithm>

#include "comms/radio_link.h"
#include "hal/hal.h"

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
}

CommandPacket RadioLink::poll() {
    CommandPacket packet{};
    const auto frame = Hal::readRcFrame();

    packet.drive.turn = clampRange(frame.normalized[0]);
    packet.drive.throttle = clampRange(frame.normalized[1]);
    const float ch3 = frame.normalized[2];
    packet.auxButton = ch3 > 0.35F;
    packet.hazard = ch3 < -0.35F;
    packet.status = modeFromChannel(frame.normalized[3]);
    packet.auxChannel5 = frame.widths[4] > 0 ? toZeroOne(frame.normalized[4]) : 1.0F;
    packet.auxChannel6 = frame.widths[5] > 0 ? toZeroOne(frame.normalized[5]) : 1.0F;
    packet.rcLinked = (frame.widths[0] > 0 || frame.widths[1] > 0);
    packet.wifiConnected = true;

    // Simple defaults: aux button toggles lighting, sound follows mode.
    packet.lightingState = packet.auxButton;
    packet.soundState = packet.status == RcStatusMode::Active;

    return packet;
}
}  // namespace TankRC::Comms
