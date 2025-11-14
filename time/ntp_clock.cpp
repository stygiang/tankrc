#include "time/ntp_clock.h"

#include <Arduino.h>

namespace TankRC::Time {
namespace {
constexpr unsigned long kRetryMs = 60000;
bool timeValid() {
    const std::uint32_t now = static_cast<std::uint32_t>(::time(nullptr));
    return now > 1609459200UL;  // Jan 1 2021
}
}

void NtpClock::configure(const Config::RuntimeConfig& config) {
    config_ = config.ntp;
    requested_ = false;
    synced_ = false;
}

void NtpClock::update(bool wifiConnected) {
    if (!wifiConnected) {
        requested_ = false;
        synced_ = false;
        return;
    }

    if (!requested_) {
        beginSync();
        return;
    }

    if (!synced_ && timeValid()) {
        synced_ = true;
    }

    if (!synced_) {
        const unsigned long nowMs = millis();
        if (nowMs - lastRequestMs_ > kRetryMs) {
            beginSync();
        }
    }
}

bool NtpClock::hasTime() const {
    return synced_ && timeValid();
}

std::uint32_t NtpClock::now() const {
    if (!hasTime()) {
        return millis() / 1000UL;
    }
    return static_cast<std::uint32_t>(::time(nullptr));
}

void NtpClock::beginSync() {
    const char* server = config_.server[0] ? config_.server : "pool.ntp.org";
    configTime(config_.gmtOffsetSeconds, config_.daylightOffsetSeconds, server);
    requested_ = true;
    lastRequestMs_ = millis();
}
}  // namespace TankRC::Time
