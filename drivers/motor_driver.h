#pragma once

namespace TankRC::Drivers {
struct ChannelPins {
    int pwm = -1;
    int in1 = -1;
    int in2 = -1;

    [[nodiscard]] bool valid() const { return pwm >= 0 && in1 >= 0 && in2 >= 0; }
};

class MotorDriver {
  public:
    void attach(const ChannelPins& motorA, const ChannelPins& motorB, int standbyPin = -1);
    void setRampRate(float unitsPerSecond);
    void setTarget(float percent);
    void update(float dtSeconds);
    void stop();

  private:
    void driveChannel(const ChannelPins& pins, float percent) const;

    ChannelPins motorA_{};
    ChannelPins motorB_{};
    int standbyPin_ = -1;
    float target_ = 0.0F;
    float current_ = 0.0F;
    float rampRate_ = 1.5F;
};
}  // namespace TankRC::Drivers
