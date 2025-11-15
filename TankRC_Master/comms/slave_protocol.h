#pragma once
#ifndef TANKRC_COMMS_SLAVE_PROTOCOL_H
#define TANKRC_COMMS_SLAVE_PROTOCOL_H

#include <cstdint>
#include <cstddef>

#include "config/runtime_config.h"

namespace TankRC::Comms::SlaveProtocol {
constexpr std::uint8_t kMagic = 0xA5;
constexpr std::size_t kMaxPayload = 96;

enum class FrameType : std::uint8_t {
    Config = 0x01,
    Command = 0x02,
    Status = 0x81,
};

#pragma pack(push, 1)
struct CommandPayload {
    float throttle = 0.0F;
    float turn = 0.0F;
};

struct StatusPayload {
    float batteryVoltage = 0.0F;
};

struct ConfigPayload {
    Config::PinAssignments pins{};
};
#pragma pack(pop)

inline std::uint8_t checksum(FrameType type, std::uint8_t length, const std::uint8_t* payload) {
    std::uint8_t sum = static_cast<std::uint8_t>(type) ^ length;
    for (std::uint8_t i = 0; i < length; ++i) {
        sum ^= payload[i];
    }
    return sum;
}
}  // namespace TankRC::Comms::SlaveProtocol
#endif  // TANKRC_COMMS_SLAVE_PROTOCOL_H
