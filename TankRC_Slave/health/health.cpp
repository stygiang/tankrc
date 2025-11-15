#include "health/health.h"

#include "hal/hal.h"

namespace TankRC::Health {
namespace {
HealthStatus status{};
}  // namespace

void setStatus(HealthCode code, const char* message, std::uint32_t timestampMs) {
    const std::uint32_t ts = timestampMs == 0 ? Hal::millis32() : timestampMs;
    status.code = code;
    status.message = message ? message : "";
    status.lastChangeMs = ts;
}

const HealthStatus& getStatus() {
    return status;
}

const char* toString(HealthCode code) {
    switch (code) {
        case HealthCode::Ok:
            return "OK";
        case HealthCode::LowBattery:
            return "Low Battery";
        default:
            return "Unknown";
    }
}
}  // namespace TankRC::Health
