#pragma once

#include <cstdint>

namespace TankRC::Events {
enum class EventType {
    DriveModeChanged,
    RcSignalLost,
    RcSignalRestored,
    LowBattery,
    BatteryRecovered,
    TipOverDetected,
    ObstacleAhead,
};

struct Event {
    EventType type;
    std::uint32_t timestampMs = 0;
    std::int32_t i1 = 0;
    float f1 = 0.0F;
};

using EventHandler = void (*)(const Event&);

void subscribe(EventHandler handler);
void publish(const Event& event);
void process();
void clear();
}  // namespace TankRC::Events
