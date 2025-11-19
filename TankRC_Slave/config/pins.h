#pragma once

namespace TankRC::Pins {
// Slave ESP32 (drive controller) hard-wiring from Pinout CSV

// UART link to master (slave side pins)
constexpr int SLAVE_UART_TX = 20;
constexpr int SLAVE_UART_RX = 21;


// I2C wiring
constexpr int PCA_SDA = 8;
constexpr int PCA_SCL = 7;
constexpr int PCF_SDA = 9;
constexpr int PCF_SCL = 10;

// PCF8575 expander pins (negative indexes, see Config::isPcfPin helpers)
constexpr int pcfPin(int index) { return -(index + 2); }

// Motor drivers (TB6612) – front driver = left track, middle driver = right track
constexpr int LEFT_MOTOR1_PWM = 6;                     // Front driver PWMA
constexpr int LEFT_MOTOR1_IN1 = pcfPin(0);             // P0 AIN1
constexpr int LEFT_MOTOR1_IN2 = pcfPin(1);             // P1 AIN2
constexpr int LEFT_MOTOR2_PWM = 5;                     // Front driver PWMB
constexpr int LEFT_MOTOR2_IN1 = pcfPin(2);             // P2 BIN1
constexpr int LEFT_MOTOR2_IN2 = pcfPin(3);             // P3 BIN2
constexpr int LEFT_DRIVER_STBY = -1;                   // STBY tied high on board

constexpr int RIGHT_MOTOR1_PWM = 1;                    // Middle driver PWMA
constexpr int RIGHT_MOTOR1_IN1 = pcfPin(4);            // P4 AIN1
constexpr int RIGHT_MOTOR1_IN2 = pcfPin(5);            // P5 AIN2
constexpr int RIGHT_MOTOR2_PWM = 2;                    // Middle driver PWMB
constexpr int RIGHT_MOTOR2_IN1 = pcfPin(6);            // P6 BIN1
constexpr int RIGHT_MOTOR2_IN2 = pcfPin(7);            // P7 BIN2
constexpr int RIGHT_DRIVER_STBY = -1;                  // STBY tied high on board
}  // namespace TankRC::Pins
