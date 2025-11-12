#pragma once
#ifndef TANKRC_DRIVERS_RC_RECEIVER_H
#define TANKRC_DRIVERS_RC_RECEIVER_H

#include <cstddef>

namespace TankRC::Drivers {
class RcReceiver {
  public:
    static constexpr std::size_t kChannelCount = 6;

    struct Frame {
        float normalized[kChannelCount]{};
        unsigned long widths[kChannelCount]{};
        unsigned long captureUs = 0;
    };

    void begin(const int* pins, std::size_t count);
    Frame readFrame();

  private:
    int pins_[kChannelCount]{};
    bool initialized_ = false;
};
}  // namespace TankRC::Drivers
#endif  // TANKRC_DRIVERS_RC_RECEIVER_H
