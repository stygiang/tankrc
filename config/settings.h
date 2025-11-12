#pragma once

namespace TankRC::Settings {
struct DriveGains {
    float kp = 0.6F;
    float ki = 0.1F;
    float kd = 0.0F;
};

struct Limits {
    float maxLinear = 1.0F;
    float maxTurn = 1.0F;
};

struct MotorDynamics {
    float rampRate = 2.5F;  // units per second (0-1 scale)
};

inline DriveGains driveGains{};
inline Limits limits{};
inline MotorDynamics motorDynamics{};
}  // namespace TankRC::Settings
