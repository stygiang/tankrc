#pragma once

#include "config/hardware_layout.h"

namespace TankRC::Hardware {
inline constexpr Hardware kEsp32_32d = {
    "ESP32-32D Main Board",
    HW_CLASS_MCU,
    BUS_LOCAL,
    0,
    20,
    {{
        {0, 15, PIN_ROLE_GPIO, false, "CH1_RC_RX"},
        {1, 2, PIN_ROLE_GPIO, false, "CH2_RC_RX"},
        {2, 4, PIN_ROLE_GPIO, false, "CH3_RC_RX"},
        {3, 16, PIN_ROLE_GPIO, false, "CH4_RC_RX"},
        {4, 17, PIN_ROLE_GPIO, false, "CH5_RC_RX"},
        {5, 5, PIN_ROLE_GPIO, false, "CH6_RC_RX"},
        {6, 1, PIN_ROLE_UART_TX, false, "UART0_TX_PARTNER"},
        {7, 3, PIN_ROLE_UART_RX, false, "UART0_RX_PARTNER"},
        {8, 18, PIN_ROLE_I2C_SDA, false, "TOF_FL_SDA"},
        {9, 19, PIN_ROLE_I2C_SCL, false, "TOF_FL_SCL"},
        {10, 22, PIN_ROLE_I2C_SDA, false, "TOF_FR_SDA"},
        {11, 23, PIN_ROLE_I2C_SCL, false, "TOF_FR_SCL"},
        {12, 14, PIN_ROLE_I2C_SCL, false, "TOF_BK_SCL"},
        {13, 27, PIN_ROLE_I2C_SDA, false, "TOF_BK_SDA"},
        {14, 26, PIN_ROLE_ADC, false, "BATTERY_VOLT"},
        {15, 25, PIN_ROLE_UNUSED, false, "OPEN_25"},
        {16, 33, PIN_ROLE_UNUSED, false, "OPEN_33"},
        {17, 32, PIN_ROLE_UNUSED, false, "OPEN_32"},
        {18, 35, PIN_ROLE_UNUSED, false, "OPEN_35"},
        {19, 34, PIN_ROLE_UNUSED, false, "OPEN_34"},
    }},
};

inline constexpr const Hardware* kMasterHardware[] = {
    &kEsp32_32d,
};

inline constexpr std::size_t kMasterHardwareCount =
    sizeof(kMasterHardware) / sizeof(kMasterHardware[0]);
}  // namespace TankRC::Hardware

namespace TankRC::Pins {
namespace detail {
template <std::size_t Index>
constexpr int boardPin() {
    static_assert(Index < Hardware::kEsp32_32d.pin_count, "ESP32-32D pin index out of range");
    return Hardware::kEsp32_32d.pins[Index].phys_num;
}
}  // namespace detail

// Primary labels lifted directly from the ESP32-32D hardware map
constexpr int CH1_RC_RX = detail::boardPin<0>();
constexpr int CH2_RC_RX = detail::boardPin<1>();
constexpr int CH3_RC_RX = detail::boardPin<2>();
constexpr int CH4_RC_RX = detail::boardPin<3>();
constexpr int CH5_RC_RX = detail::boardPin<4>();
constexpr int CH6_RC_RX = detail::boardPin<5>();

constexpr int UART0_TX_PARTNER = detail::boardPin<6>();
constexpr int UART0_RX_PARTNER = detail::boardPin<7>();

constexpr int TOF_FL_SDA = detail::boardPin<8>();
constexpr int TOF_FL_SCL = detail::boardPin<9>();
constexpr int TOF_FR_SDA = detail::boardPin<10>();
constexpr int TOF_FR_SCL = detail::boardPin<11>();
constexpr int TOF_BK_SCL = detail::boardPin<12>();
constexpr int TOF_BK_SDA = detail::boardPin<13>();

constexpr int BATTERY_VOLT = detail::boardPin<14>();

constexpr int OPEN_25 = detail::boardPin<15>();
constexpr int OPEN_33 = detail::boardPin<16>();
constexpr int OPEN_32 = detail::boardPin<17>();
constexpr int OPEN_35 = detail::boardPin<18>();
constexpr int OPEN_34 = detail::boardPin<19>();

// Derived aliases used throughout the firmware (kept for compatibility)
constexpr int STATUS_LED = OPEN_25;
constexpr int RC_CH1 = CH1_RC_RX;
constexpr int RC_CH2 = CH2_RC_RX;
constexpr int RC_CH3 = CH3_RC_RX;
constexpr int RC_CH4 = CH4_RC_RX;
constexpr int RC_CH5 = CH5_RC_RX;
constexpr int RC_CH6 = CH6_RC_RX;
constexpr int SLAVE_UART_TX = UART0_TX_PARTNER;
constexpr int SLAVE_UART_RX = UART0_RX_PARTNER;
constexpr int BATTERY_SENSE = BATTERY_VOLT;
constexpr int LIGHT_BAR = -1;
constexpr int SPEAKER = -1;

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
