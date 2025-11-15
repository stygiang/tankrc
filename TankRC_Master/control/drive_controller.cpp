#include <Arduino.h>

#include "config/settings.h"
#include "control/drive_controller.h"

namespace TankRC::Control {
void DriveController::begin(const Config::RuntimeConfig& config) {
    config_ = &config;
    slave_.begin(config);
}

void DriveController::setCommand(const Comms::DriveCommand& command) {
    command_ = command;
    slave_.setCommand(command_);
}

void DriveController::setLightingCommand(const Comms::SlaveProtocol::LightingCommand& lighting) {
    slave_.setLightingCommand(lighting);
}

void DriveController::update() {
    slave_.update();
}

float DriveController::readBatteryVoltage() {
    return slave_.batteryVoltage();
}
#if defined(TANKRC_BUILD_MASTER) && !TANKRC_USE_DRIVE_PROXY
#error "Master firmware requires TANKRC_USE_DRIVE_PROXY=1"
#endif
}  // namespace TankRC::Control
