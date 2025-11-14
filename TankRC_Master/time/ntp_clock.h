#pragma once

#include <ctime>

#include "config/runtime_config.h"

namespace TankRC::Time {
class NtpClock {
  public:
    void configure(const Config::RuntimeConfig& config);
    void update(bool wifiConnected);
    bool hasTime() const;
    std::uint32_t now() const;  // seconds since epoch

  private:
    void beginSync();

    Config::NtpConfig config_{};
    bool requested_ = false;
    bool synced_ = false;
    unsigned long lastRequestMs_ = 0UL;
};
}  // namespace TankRC::Time
