#pragma once

namespace TankRC::Control {
class PID {
  public:
    void configure(float kp, float ki, float kd);
    float update(float error, float dt);
    void reset();

  private:
    float kp_ = 0.0F;
    float ki_ = 0.0F;
    float kd_ = 0.0F;
    float integral_ = 0.0F;
    float prevError_ = 0.0F;
};
}  // namespace TankRC::Control
