# Software Architecture

1. **Core bring-up (`core/`)** initializes clocks, peripherals, and shared services.
2. **Drivers (`drivers/`)** expose hardware features (e.g., TB6612FNG dual-motor driver with ramped outputs, battery monitor) behind clean C++ interfaces.
3. **Control (`control/`)** implements motion logic and shared control algorithms.
4. **Comms (`comms/`)** handles radio/telemetry links.
5. **Features (`features/`)** hold user-facing modules such as lighting and sound.
6. **Config (`config/`)** centralizes tunables like pins, PID gains, and safety limits, and now includes `runtime_config` for user-editable pin maps.
7. **Storage/UI (`storage/`, `ui/`)** provide persistence via ESP32 Preferences and a serial console wizard for configuring/testing the RC platform in the field.

Each layer only depends on the ones above it to keep compile times down and enable reuse on other vehicles.
