#include "logging/session_logger.h"

namespace TankRC::Logging {
void SessionLogger::configure(const Config::LoggingConfig& config) {
    config_ = config;
    buffer_.assign(config_.maxEntries ? config_.maxEntries : 1, {});
    head_ = 0;
    count_ = 0;
}

void SessionLogger::clear() {
    head_ = 0;
    count_ = 0;
}

void SessionLogger::log(const LogEntry& entry) {
    if (!config_.enabled || buffer_.empty()) {
        return;
    }
    buffer_[head_] = entry;
    head_ = (head_ + 1) % buffer_.size();
    if (count_ < buffer_.size()) {
        ++count_;
    }
}

std::vector<LogEntry> SessionLogger::entries() const {
    std::vector<LogEntry> out;
    out.reserve(count_);
    for (std::size_t i = 0; i < count_; ++i) {
        std::size_t index = (head_ + buffer_.size() - count_ + i) % buffer_.size();
        out.push_back(buffer_[index]);
    }
    return out;
}
}  // namespace TankRC::Logging
