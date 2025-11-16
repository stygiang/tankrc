#include "config/runtime_config.h"

#include <cstring>

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
    config.pins.slaveTx = Pins::SLAVE_UART_TX;
    config.pins.slaveRx = Pins::SLAVE_UART_RX;

    config.features.lightsEnabled = FEATURE_LIGHTS != 0;
    config.features.soundEnabled = FEATURE_SOUND != 0;
    config.features.sensorsEnabled = FEATURE_ULTRASONIC != 0;
    config.features.wifiEnabled = FEATURE_WIFI != 0;
    config.features.ultrasonicEnabled = FEATURE_ULTRASONIC != 0;
    config.features.tipOverEnabled = FEATURE_TIPOVER != 0;

    config.lighting.pcaAddress = 0x40;
    config.lighting.pwmFrequency = 800;
    config.lighting.channels.frontLeft = {0, 1, 2};
    config.lighting.channels.frontRight = {3, 4, 5};
    config.lighting.channels.rearLeft = {6, 7, 8};
    config.lighting.channels.rearRight = {9, 10, 11};
    config.lighting.blink.periodMs = 450;
    config.lighting.blink.wifi = true;
    config.lighting.blink.rc = true;

    std::strncpy(config.wifi.ssid, "", sizeof(config.wifi.ssid));
    std::strncpy(config.wifi.password, "", sizeof(config.wifi.password));
    std::strncpy(config.wifi.apSsid, "TankRC-Setup", sizeof(config.wifi.apSsid));
    std::strncpy(config.wifi.apPassword, "tankrc123", sizeof(config.wifi.apPassword));
    config.wifi.ssid[sizeof(config.wifi.ssid) - 1] = '\0';
    config.wifi.password[sizeof(config.wifi.password) - 1] = '\0';
    config.wifi.apSsid[sizeof(config.wifi.apSsid) - 1] = '\0';
    config.wifi.apPassword[sizeof(config.wifi.apPassword) - 1] = '\0';

    std::strncpy(config.ntp.server, "pool.ntp.org", sizeof(config.ntp.server));
    config.ntp.server[sizeof(config.ntp.server) - 1] = '\0';
    config.ntp.gmtOffsetSeconds = 0;
    config.ntp.daylightOffsetSeconds = 0;

    config.logging.enabled = true;
    config.logging.maxEntries = 512;

    config.rc.channelPins[0] = Pins::RC_CH1;
    config.rc.channelPins[1] = Pins::RC_CH2;
    config.rc.channelPins[2] = Pins::RC_CH3;
    config.rc.channelPins[3] = Pins::RC_CH4;
    config.rc.channelPins[4] = Pins::RC_CH5;
    config.rc.channelPins[5] = Pins::RC_CH6;

    return config;
}
}  // namespace TankRC::Config
