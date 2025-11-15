#pragma once

namespace TankRC::Pins {
// Left driver (TB6612FNG #1)
constexpr int LEFT_MOTOR1_PWM = 25;
constexpr int LEFT_MOTOR1_IN1 = 26;
constexpr int LEFT_MOTOR1_IN2 = 27;
constexpr int LEFT_MOTOR2_PWM = 32;
constexpr int LEFT_MOTOR2_IN1 = 33;
constexpr int LEFT_MOTOR2_IN2 = 14;
constexpr int LEFT_DRIVER_STBY = 5;

// Right driver (TB6612FNG #2)
constexpr int RIGHT_MOTOR1_PWM = 16;
constexpr int RIGHT_MOTOR1_IN1 = 17;
constexpr int RIGHT_MOTOR1_IN2 = 18;
constexpr int RIGHT_MOTOR2_PWM = 19;
constexpr int RIGHT_MOTOR2_IN1 = 12;
constexpr int RIGHT_MOTOR2_IN2 = 13;
constexpr int RIGHT_DRIVER_STBY = 23;

// RC receiver channels (default wiring)
constexpr int RC_CH1 = 35;  // steering
constexpr int RC_CH2 = 34;  // throttle
constexpr int RC_CH3 = 39;  // aux button
constexpr int RC_CH4 = 36;  // 3-way switch
constexpr int RC_CH5 = -1;  // reserved
constexpr int RC_CH6 = -1;  // reserved

constexpr int BATTERY_SENSE = 34;
constexpr int IMU_SDA = 21;
constexpr int IMU_SCL = 22;
constexpr int STATUS_LED = 2;
constexpr int LIGHT_BAR = 15;
constexpr int SPEAKER = 4;
constexpr int SLAVE_UART_RX = 16;
constexpr int SLAVE_UART_TX = 17;
}  // namespace TankRC::Pins
