# Hardware Notes

- Chassis: tracked/tank platform with four 12 V motors (two per track) for high torque.
- Motor drivers: pair of TB6612FNG dual H-bridge boards; each board carries one track and exposes PWMA/B+IN1/2 pins plus STBY as defined in `config/pins.h`.
- RC input: 6-channel PWM receiver (steering/throttle/button/mode switch + two ultrasonic channels) wired to the pins listed in `config/pins.h` and adjustable via the serial wizard.
- Lighting: PCA9685 16-channel PWM expander driving four RGB fixtures (headlights + reverse lights). Each RGB line takes its own PCA channel, so make sure the board’s address/jumpers match the config. Ultrasonic readings (CH5/CH6) tint the headlights via a green→yellow→red gradient.
- Networking: ESP32 station mode + fallback AP (`TankRC-Setup`) so you can access the web control panel even away from infrastructure Wi-Fi.
- Runtime pin assignment: pins are configurable at runtime via the serial wizard (no rebuild required) so wire however is convenient and just map them later.
- Sensors: IMU (yaw/pitch), battery voltage divider, optional range finder.
- Radio link: 2.4 GHz RC receiver or ESP-NOW based handset.
- Aux features: LED lighting, sound module.

Document wiring and part numbers here so the firmware modules stay hardware agnostic.
