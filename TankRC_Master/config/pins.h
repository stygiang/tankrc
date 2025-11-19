#pragma once

namespace TankRC::Pins {
// Master ESP32 default GPIO assignments
constexpr int  = 25;
constexpr int  = 26;
constexpr int = 27;
constexpr int  = 32;
constexpr int  = 33;
constexpr int  = 14;
constexpr int  = 5;

constexpr int  = 16;
constexpr int  = 17;
constexpr int  = 18;
constexpr int = 19;
constexpr int  = 12;
constexpr int  = 13;
constexpr int  = 23;

// RC receiver channels (default wiring)
constexpr int RC_CH1 = 35;  // steering
constexpr int RC_CH2 = 34;  // throttle
constexpr int RC_CH3 = 39;  // aux button
constexpr int RC_CH4 = 36;  // 3-way switch
constexpr int RC_CH5 = -1;  // reserved
constexpr int RC_CH6 = -1;  // reserved

// Sensors / helpers
constexpr int BATTERY_SENSE = 34;
constexpr int IMU_SDA = 21;
constexpr int IMU_SCL = 22;
constexpr int  = 2;

// Lighting & sound
constexpr int  = 15;
constexpr int  = 4;

// UART link
constexpr int SLAVE_UART_RX = 16;
constexpr int SLAVE_UART_TX = 17;
}  // namespace TankRC::Pins
