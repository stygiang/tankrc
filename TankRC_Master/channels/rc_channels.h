#pragma once

#include <cstddef>

#include "drivers/rc_receiver.h"

namespace TankRC::Channels {
enum class RcChannel : std::size_t {
    Steering = 0,
    Throttle = 1,
    AuxPrimary = 2,
    Mode = 3,
    Aux5 = 4,
    Aux6 = 5,
};

inline float readNormalized(const Drivers::RcReceiver::Frame& frame, RcChannel channel) {
    const std::size_t idx = static_cast<std::size_t>(channel);
    if (idx >= Drivers::RcReceiver::kChannelCount) {
        return 0.0F;
    }
    return frame.normalized[idx];
}

inline unsigned long readWidth(const Drivers::RcReceiver::Frame& frame, RcChannel channel) {
    const std::size_t idx = static_cast<std::size_t>(channel);
    if (idx >= Drivers::RcReceiver::kChannelCount) {
        return 0UL;
    }
    return frame.widths[idx];
}
}  // namespace TankRC::Channels
