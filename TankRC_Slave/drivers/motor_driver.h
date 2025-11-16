#pragma once

#include <cstdint>

namespace TankRC::Drivers {
class IoExpander;

enum class PinSource : std::uint8_t { None = 0, Gpio, IoExpander };

struct DigitalPin {
    PinSource source = PinSource::None;
    int gpio = -1;
    std::uint8_t expanderPin = 0;

    [[nodiscard]] bool valid() const {
        switch (source) {
            case PinSource::Gpio:
                return gpio >= 0;
            case PinSource::IoExpander:
                return true;
            default:
                return false;
        }
    }
};

struct ChannelPins {
    int pwm = -1;
    DigitalPin in1{};
    DigitalPin in2{};

    [[nodiscard]] bool valid() const { return pwm >= 0 && in1.valid() && in2.valid(); }
};

class MotorDriver {
  public:
    void attach(const ChannelPins& motorA,
               const ChannelPins& motorB,
               const DigitalPin& standbyPin,
               IoExpander* expander = nullptr);
    void setRampRate(float unitsPerSecond);
    void setTarget(float percent);
    void update(float dtSeconds);
    void stop();

  private:
    void driveChannel(const ChannelPins& pins, float percent) const;
    void writeDigital(const DigitalPin& pin, bool high) const;

    ChannelPins motorA_{};
    ChannelPins motorB_{};
    DigitalPin standbyPin_{};
    IoExpander* expander_ = nullptr;
    float target_ = 0.0F;
    float current_ = 0.0F;
    float rampRate_ = 1.5F;
};
}  // namespace TankRC::Drivers
