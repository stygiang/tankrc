#include "control/pid.h"

namespace TankRC::Control {
void PID::configure(float kp, float ki, float kd) {
    kp_ = kp;
    ki_ = ki;
    kd_ = kd;
}

float PID::update(float error, float dt) {
    integral_ += error * dt;
    const float derivative = (error - prevError_) / dt;
    prevError_ = error;
    return (kp_ * error) + (ki_ * integral_) + (kd_ * derivative);
}

void PID::reset() {
    integral_ = 0.0F;
    prevError_ = 0.0F;
}
}  // namespace TankRC::Control
