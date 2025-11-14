#pragma once

#include "comms/radio_link.h"
#include "config/runtime_config.h"
#include "control/pid.h"
#include "drivers/battery_monitor.h"
#include "drivers/motor_driver.h"

namespace TankRC::Control {
class DriveController {
  public:
    void begin(const Config::RuntimeConfig& config);
    void setCommand(const Comms::DriveCommand& command);
    void update();
    float readBatteryVoltage();

  private:
    const Config::RuntimeConfig* config_ = nullptr;
    Comms::DriveCommand command_{};
    Drivers::MotorDriver leftMotor_{};
    Drivers::MotorDriver rightMotor_{};
    Drivers::BatteryMonitor battery_{};
    PID leftPid_{};
    PID rightPid_{};
    unsigned long lastUpdateMs_ = 0UL;
};
}  // namespace TankRC::Control
