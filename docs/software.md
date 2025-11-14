# Software Architecture

1. **Core bring-up (`core/`)** initializes clocks, peripherals, and shared services.
2. **Drivers (`drivers/`)** expose hardware features (e.g., TB6612FNG dual-motor driver with ramped outputs, RC receiver pulse capture, battery monitor) behind clean C++ interfaces.
3. **Control (`control/`)** implements motion logic and shared control algorithms.
4. **Comms (`comms/`)** handles radio/telemetry links—the default `RadioLink` now translates RC receiver channels into throttle/steering, mode (Debug/Active/Locked), and auxiliary button states.
5. **Features (`features/`)** hold user-facing modules such as lighting and sound. The lighting stack consumes the PCA9685 driver, auto-manages headlights/turn signals/reverse lamps, hazards, connectivity chase patterns, and ultrasonic-based color gradients.
6. **Config (`config/`)** centralizes tunables like pins, PID gains, and safety limits, and now includes `runtime_config` for user-editable pin maps.
7. **Storage/UI (`storage/`, `ui/`)** provide persistence plus serial configuration wizards.
8. **Network (`network/`)** handles Wi-Fi connections (station + AP fallback), captive-style onboarding, NTP time sync, and hosts the in-browser “TankRC Control Hub” with live telemetry, manual overrides, mirrored configuration forms, and downloadable run logs. This layer is wrapped behind `TANKRC_ENABLE_NETWORK`; leave it disabled for barebones RC testing and re-enable when you’re ready for Wi-Fi features.
9. **Logging/Time (`logging/`, `time/`)** capture timestamped drive samples (CSV/JSON export) and manage NTP synchronization so every log is tied to real-world time.

Each layer only depends on the ones above it to keep compile times down and enable reuse on other vehicles.
