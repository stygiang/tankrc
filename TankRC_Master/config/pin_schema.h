#pragma once

// Pin layout is captured via config/hardware_map.h; values come directly from the
// hardware CSVs in `/Users/drk/Downloads/Private & Shared/Pinout/`.
// - Master (controller): RC CH1-6 on GPIO 15/2/4/16/17/5, UART to slave on TX0/RX0,
//   battery sense on GPIO 26, status LED on GPIO 25.
// - Slave (drive): TB6612 drivers fed by PWM GPIO 6/5 (front) and 1/2 (middle),
//   direction lines on PCF8575 P0–P7, UART to master on GPIO 20/21,
//   PCA9685 (lights) on SDA 8 / SCL 7, PCF8575 on SDA 9 / SCL 10.
// Any future schema changes should be reflected there so the runtime config
// defaults always match the CAD pinout.
