#pragma once

#include "config/runtime_config.h"

namespace TankRC::Features {
enum class LightId { HeadLeft, HeadRight, TailLeft, TailRight };

inline const Config::RgbChannel& channelFor(const Config::LightingChannelMap& map, LightId id) {
    switch (id) {
        case LightId::HeadLeft:
            return map.frontLeft;
        case LightId::HeadRight:
            return map.frontRight;
        case LightId::TailLeft:
            return map.rearLeft;
        case LightId::TailRight:
        default:
            return map.rearRight;
    }
}
}  // namespace TankRC::Features
