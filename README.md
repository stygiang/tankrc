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
- **CH3** → 3-position aux (up = hazard flashers, middle = lights off, down = lights on)
- **CH4** → 3-way switch mapped to `Debug`, `Active`, and `Locked` drive modes
- **CH5/CH6** → ultrasonic sensor readings (0–1 normalized distance) that tint the headlights green→yellow→red as obstacles approach

All receiver pins can be reassigned from the serial wizard, so feel free to wire them wherever it's convenient on your ESP32.

## PCA9685 lighting system
Four RGB assemblies (front-left/right headlights and rear-left/right reverse lights) connect through a PCA9685 PWM expander—each color component maps to its own channel. The lighting controller handles:
- **Mode colors:** Debug, Active, and Locked each broadcast a distinct color palette so status is visible at a glance.
- **Auto turn signals:** Steering left/right past a threshold starts a timed amber blink on the corresponding side.
- **Reverse lights:** Rear lights flip to bright white automatically while backing up.
- **Hazard mode:** CH3 up (or a configured sensor fault) triggers a double-blink pattern on both turn signals + rear lights.
- **Connectivity blink codes:** Wi-Fi, RC link, and Bluetooth each trigger their own chase pattern so you can tell which link dropped.
- **Sensor tinting:** CH5/CH6 (ultrasonic) modulate the headlight color from green (clear) to red (danger) as obstacles get closer.

Address, frequency, RGB channel assignments, and blink behaviors are all editable from the serial wizard so you can match whatever wiring layout you prefer.
