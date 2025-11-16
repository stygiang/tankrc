#pragma once

#include "config/runtime_config.h"

namespace TankRC::Drivers {
class Pcf8575;
struct ChannelPins {
    int pwm = -1;
    int in1 = -1;
    int in2 = -1;

    [[nodiscard]] bool assigned(int pin) const { return pin >= 0 || Config::isPcfPin(pin); }
    [[nodiscard]] bool valid() const { return pwm >= 0 && assigned(in1) && assigned(in2); }
};

class MotorDriver {
  public:
    void attach(const ChannelPins& motorA, const ChannelPins& motorB, int standbyPin = -1, Pcf8575* expander = nullptr);
    void setRampRate(float unitsPerSecond);
    void setTarget(float percent);
    void update(float dtSeconds);
    void stop();

  private:
    void driveChannel(const ChannelPins& pins, float percent) const;
    void writeDigital(int pin, bool high) const;

    ChannelPins motorA_{};
    ChannelPins motorB_{};
    int standbyPin_ = -1;
    Pcf8575* expander_ = nullptr;
    float target_ = 0.0F;
    float current_ = 0.0F;
    float rampRate_ = 1.5F;
};
}  // namespace TankRC::Drivers
