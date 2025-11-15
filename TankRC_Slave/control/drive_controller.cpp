#include <Arduino.h>

#include "config/settings.h"
#include "control/drive_controller.h"

namespace TankRC::Control {
#if TANKRC_USE_DRIVE_PROXY
void DriveController::begin(const Config::RuntimeConfig& config) {
    config_ = &config;
    slave_.begin(config);
}

void DriveController::setCommand(const Comms::DriveCommand& command) {
    command_ = command;
    slave_.setCommand(command_);
}

void DriveController::update() {
    slave_.update();
}

float DriveController::readBatteryVoltage() {
    return slave_.batteryVoltage();
}
#else
void DriveController::begin(const Config::RuntimeConfig& config) {
    config_ = &config;
    leftPid_.configure(Settings::driveGains.kp, Settings::driveGains.ki, Settings::driveGains.kd);
    rightPid_.configure(Settings::driveGains.kp, Settings::driveGains.ki, Settings::driveGains.kd);
    lastUpdateMs_ = Hal::millis32();
}

void DriveController::setCommand(const Comms::DriveCommand& command) {
    command_ = command;
}

void DriveController::update() {
    const unsigned long now = Hal::millis32();
    const float dt = (now - lastUpdateMs_) / 1000.0F;
    lastUpdateMs_ = now;

    float throttle = constrain(command_.throttle, -Settings::limits.maxLinear, Settings::limits.maxLinear);
    float turn = constrain(command_.turn, -Settings::limits.maxTurn, Settings::limits.maxTurn);

    const float leftTarget = throttle - turn;
    const float rightTarget = throttle + turn;

    const float leftOut = leftPid_.update(leftTarget, dt);
    const float rightOut = rightPid_.update(rightTarget, dt);

    Hal::setMotorOutputs(leftOut, rightOut);
    Hal::updateMotorController(dt);

    const float voltage = Hal::readBatteryVoltage();
    if (voltage < 11.0F) {
        Hal::stopMotors();
    }
}

float DriveController::readBatteryVoltage() {
    return Hal::readBatteryVoltage();
}
#endif
}  // namespace TankRC::Control
