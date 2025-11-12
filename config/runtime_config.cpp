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

    config.rc.channelPins[0] = Pins::RC_CH1;
    config.rc.channelPins[1] = Pins::RC_CH2;
    config.rc.channelPins[2] = Pins::RC_CH3;
    config.rc.channelPins[3] = Pins::RC_CH4;
    config.rc.channelPins[4] = Pins::RC_CH5;
    config.rc.channelPins[5] = Pins::RC_CH6;

    return config;
}
}  // namespace TankRC::Config
