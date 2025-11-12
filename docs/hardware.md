# Hardware Notes

- Chassis: tracked/tank platform with four 12 V motors (two per track) for high torque.
- Motor drivers: pair of TB6612FNG dual H-bridge boards; each board carries one track and exposes PWMA/B+IN1/2 pins plus STBY as defined in `config/pins.h`.
- Runtime pin assignment: pins are configurable at runtime via the serial wizard (no rebuild required) so wire however is convenient and just map them later.
- Sensors: IMU (yaw/pitch), battery voltage divider, optional range finder.
- Radio link: 2.4 GHz RC receiver or ESP-NOW based handset.
- Aux features: LED lighting, sound module.

Document wiring and part numbers here so the firmware modules stay hardware agnostic.
