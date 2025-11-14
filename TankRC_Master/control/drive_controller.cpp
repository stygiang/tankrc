#include <Arduino.h>

#include "config/settings.h"
#include "control/drive_controller.h"

namespace TankRC::Control {
void DriveController::begin(const Config::RuntimeConfig& config) {
    config_ = &config;

    Drivers::ChannelPins leftMotorA{config.pins.leftDriver.motorA.pwm,
                                    config.pins.leftDriver.motorA.in1,
                                    config.pins.leftDriver.motorA.in2};
    Drivers::ChannelPins leftMotorB{config.pins.leftDriver.motorB.pwm,
                                    config.pins.leftDriver.motorB.in1,
                                    config.pins.leftDriver.motorB.in2};
    leftMotor_.attach(leftMotorA, leftMotorB, config.pins.leftDriver.standby);
    leftMotor_.setRampRate(Settings::motorDynamics.rampRate);

    Drivers::ChannelPins rightMotorA{config.pins.rightDriver.motorA.pwm,
                                     config.pins.rightDriver.motorA.in1,
                                     config.pins.rightDriver.motorA.in2};
    Drivers::ChannelPins rightMotorB{config.pins.rightDriver.motorB.pwm,
                                     config.pins.rightDriver.motorB.in1,
                                     config.pins.rightDriver.motorB.in2};
    rightMotor_.attach(rightMotorA, rightMotorB, config.pins.rightDriver.standby);
    rightMotor_.setRampRate(Settings::motorDynamics.rampRate);

    battery_.attach(config.pins.batterySense, 2.0F);
    leftPid_.configure(Settings::driveGains.kp, Settings::driveGains.ki, Settings::driveGains.kd);
    rightPid_.configure(Settings::driveGains.kp, Settings::driveGains.ki, Settings::driveGains.kd);
    lastUpdateMs_ = millis();
}

void DriveController::setCommand(const Comms::DriveCommand& command) {
    command_ = command;
}

void DriveController::update() {
    const unsigned long now = millis();
    const float dt = (now - lastUpdateMs_) / 1000.0F;
    lastUpdateMs_ = now;

    float throttle = constrain(command_.throttle, -Settings::limits.maxLinear, Settings::limits.maxLinear);
    float turn = constrain(command_.turn, -Settings::limits.maxTurn, Settings::limits.maxTurn);

    const float leftTarget = throttle - turn;
    const float rightTarget = throttle + turn;

    const float leftOut = leftPid_.update(leftTarget, dt);
    const float rightOut = rightPid_.update(rightTarget, dt);

    leftMotor_.setTarget(leftOut);
    rightMotor_.setTarget(rightOut);
    leftMotor_.update(dt);
    rightMotor_.update(dt);

    const float voltage = battery_.readVoltage();
    if (voltage < 11.0F) {
        leftMotor_.stop();
        rightMotor_.stop();
    }
}

float DriveController::readBatteryVoltage() {
    return battery_.readVoltage();
}
}  // namespace TankRC::Control
