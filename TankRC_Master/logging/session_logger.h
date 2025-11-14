#pragma once
#ifndef TANKRC_LOGGING_SESSION_LOGGER_H
#define TANKRC_LOGGING_SESSION_LOGGER_H

#include <vector>

#include "comms/radio_link.h"
#include "config/runtime_config.h"

namespace TankRC::Logging {
struct LogEntry {
    std::uint32_t epoch = 0;
    float throttle = 0.0F;
    float steering = 0.0F;
    bool hazard = false;
    Comms::RcStatusMode mode = Comms::RcStatusMode::Active;
    float battery = 0.0F;
};

class SessionLogger {
  public:
    void configure(const Config::LoggingConfig& config);
    void clear();
    void log(const LogEntry& entry);
    std::vector<LogEntry> entries() const;
    std::size_t size() const { return count_; }
    bool enabled() const { return config_.enabled; }

  private:
    Config::LoggingConfig config_{};
    std::vector<LogEntry> buffer_;
    std::size_t head_ = 0;
    std::size_t count_ = 0;
};
}  // namespace TankRC::Logging
#endif  // TANKRC_LOGGING_SESSION_LOGGER_H
