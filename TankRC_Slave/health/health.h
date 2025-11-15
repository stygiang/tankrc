#pragma once

#include <cstdint>

namespace TankRC::Health {
enum class HealthCode {
    Ok = 0,
    LowBattery,
};

struct HealthStatus {
    HealthCode code = HealthCode::Ok;
    const char* message = "All systems nominal";
    std::uint32_t lastChangeMs = 0;
};

void setStatus(HealthCode code, const char* message, std::uint32_t timestampMs);
inline void setStatus(HealthCode code, const char* message) {
    setStatus(code, message, 0);
}

const HealthStatus& getStatus();
const char* toString(HealthCode code);
}  // namespace TankRC::Health
