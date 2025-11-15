#include <Arduino.h>
#include <algorithm>

#include "comms/radio_link.h"
#include "channels/rc_channels.h"
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

    packet.drive.turn = clampRange(Channels::readNormalized(frame, Channels::RcChannel::Steering));
    packet.drive.throttle = clampRange(Channels::readNormalized(frame, Channels::RcChannel::Throttle));
    const float auxPrimary = Channels::readNormalized(frame, Channels::RcChannel::AuxPrimary);
    packet.auxButton = auxPrimary > 0.35F;
    packet.hazard = auxPrimary < -0.35F;
    packet.status = modeFromChannel(Channels::readNormalized(frame, Channels::RcChannel::Mode));
    const unsigned long aux5Width = Channels::readWidth(frame, Channels::RcChannel::Aux5);
    const unsigned long aux6Width = Channels::readWidth(frame, Channels::RcChannel::Aux6);
    packet.auxChannel5 = aux5Width > 0 ? toZeroOne(Channels::readNormalized(frame, Channels::RcChannel::Aux5)) : 1.0F;
    packet.auxChannel6 = aux6Width > 0 ? toZeroOne(Channels::readNormalized(frame, Channels::RcChannel::Aux6)) : 1.0F;
    packet.rcLinked = (Channels::readWidth(frame, Channels::RcChannel::Steering) > 0 ||
                       Channels::readWidth(frame, Channels::RcChannel::Throttle) > 0);
    packet.wifiConnected = true;

    // Simple defaults: aux button toggles lighting, sound follows mode.
    packet.lightingState = packet.auxButton;
    packet.soundState = packet.status == RcStatusMode::Active;

    return packet;
}
}  // namespace TankRC::Comms
