#pragma once

#include "comms/radio_link.h"
#include "config/runtime_config.h"

#ifndef TANKRC_USE_DRIVE_PROXY
#if defined(TANKRC_BUILD_MASTER) && !defined(TANKRC_BUILD_SLAVE)
#define TANKRC_USE_DRIVE_PROXY 1
#else
#define TANKRC_USE_DRIVE_PROXY 0
#endif
#endif

#if TANKRC_USE_DRIVE_PROXY
#include "comms/slave_link.h"
#else
#include "control/pid.h"
#include "drivers/battery_monitor.h"
#include "drivers/motor_driver.h"
#endif

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
#if TANKRC_USE_DRIVE_PROXY
    Comms::SlaveLink slave_;
#else
    Drivers::MotorDriver leftMotor_{};
    Drivers::MotorDriver rightMotor_{};
    Drivers::BatteryMonitor battery_{};
    PID leftPid_{};
    PID rightPid_{};
    unsigned long lastUpdateMs_ = 0UL;
#endif
};
}  // namespace TankRC::Control
