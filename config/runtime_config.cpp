#include "config/runtime_config.h"

#include "config/pins.h"

namespace TankRC::Config {
RuntimeConfig makeDefaultConfig() {
    RuntimeConfig config{};

    config.pins.leftDriver.motorA = {Pins::LEFT_MOTOR1_PWM, Pins::LEFT_MOTOR1_IN1, Pins::LEFT_MOTOR1_IN2};
    config.pins.leftDriver.motorB = {Pins::LEFT_MOTOR2_PWM, Pins::LEFT_MOTOR2_IN1, Pins::LEFT_MOTOR2_IN2};
    config.pins.leftDriver.standby = Pins::LEFT_DRIVER_STBY;

    config.pins.rightDriver.motorA = {Pins::RIGHT_MOTOR1_PWM, Pins::RIGHT_MOTOR1_IN1, Pins::RIGHT_MOTOR1_IN2};
    config.pins.rightDriver.motorB = {Pins::RIGHT_MOTOR2_PWM, Pins::RIGHT_MOTOR2_IN1, Pins::RIGHT_MOTOR2_IN2};
    config.pins.rightDriver.standby = Pins::RIGHT_DRIVER_STBY;

    config.pins.lightBar = Pins::LIGHT_BAR;
    config.pins.speaker = Pins::SPEAKER;
    config.pins.batterySense = Pins::BATTERY_SENSE;

    config.features.lightingEnabled = true;
    config.features.soundEnabled = true;
    config.features.sensorsEnabled = true;

    return config;
}
}  // namespace TankRC::Config
