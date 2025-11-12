# TankRC

Modular ESP-based tank-style RC platform. This repo focuses on keeping each feature isolated in its own component so the project scales without turning into a monolithic sketch.

## Layout
- `tankrc.ino` – Arduino IDE entry point that wires up the runtime configuration wizard.
- `tankrc_modules.cpp` – Pulls in every module implementation so the Arduino build system compiles the deeper folder structure without extra setup.
- `TankRC.h`, `config/`, `control/`, `drivers/`, etc. – Core firmware organized by concern (living alongside the sketch so the Arduino IDE can find them).
- `docs/` – System and hardware notes.
- `scripts/` – Helper scripts for building/flashing/testing.
- `tests/` – Unit/integration tests and harness configs.
- `tools/` – Desktop utilities, simulators, calibration helpers.

Open `tankrc.ino` inside the Arduino IDE (the folder name already matches) and the IDE will pull in every `.cpp/.h` that sits next to the sketch.

## Serial configuration & test console
Open a serial monitor at 115200 baud and type `help` to launch the interactive wizard. You can:
- Reassign any motor/feature pins without recompiling (`wizard pins`), then save them to flash.
- Enable/disable feature modules (`wizard features`), useful when hardware isn't installed yet.
- Run the bundled test suite (`wizard test`) to sweep the tank drive, blink the light bar, pulse the speaker, and read battery voltage before heading into the field.

Settings survive power cycles via the on-board NVS/Preferences store, and you can revert to defaults anytime with the `defaults` command.

## RC receiver mapping
The built-in six-channel receiver driver interprets standard 1–2 ms PWM signals:
- **CH1** → steering (turn command, -1.0 to 1.0)
- **CH2** → throttle (drive command, -1.0 to 1.0)
- **CH3** → momentary/aux button (currently toggles lighting)
- **CH4** → 3-way switch mapped to `Debug`, `Active`, and `Locked` drive modes
- **CH5/CH6** → reserved for upcoming ultrasonic sensor control (values exposed to the app for future logic)

All receiver pins can be reassigned from the serial wizard, so feel free to wire them wherever it's convenient on your ESP32.
