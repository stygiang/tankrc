# TankRC

Modular ESP-based tank-style RC platform. This repo focuses on keeping each feature isolated in its own component so the project scales without turning into a monolithic sketch.

## Layout
- `TankRC_Master/TankRC_Master.ino` – Main ESP (master) sketch that wires up the runtime configuration wizard. Networking is disabled by default; define `TANKRC_ENABLE_NETWORK=1` before including `TankRC.h` (or via `platformio.ini`) to turn on Wi‑Fi/web control.
- `TankRC_Slave/TankRC_Slave.ino` – Drive/motor controller firmware that the master streams commands to over UART. Flash this to the ESP32 that physically hosts the TB6612s and battery monitor.
- `tankrc_modules.cpp` – Pulls in every module implementation so the Arduino build system compiles the deeper folder structure without extra setup.
- `TankRC.h`, `config/`, `control/`, `drivers/`, `features/`, `network/`, `logging/`, `time/` – Shared firmware modules (now copied inside `TankRC_Master/` so the Arduino IDE can compile everything from a single sketch folder).
- `docs/` – System and hardware notes.
- `scripts/` – Helper scripts for building/flashing/testing.
- `tests/` – Unit/integration tests and harness configs.
- `tools/` – Desktop utilities, simulators, calibration helpers.

Open `tankrc.ino` inside the Arduino IDE (the folder name already matches) and the IDE will pull in every `.cpp/.h` that sits next to the sketch.

## Serial configuration & test console
Open a serial monitor at 115200 baud and type `help` to see the slimmed-down console dashboard. The serial wizard now focuses on feature control and diagnostics: pin assignments and ownership are managed through the web UI (see below).
- `menu` launches the new dashboard that exposes feature toggles and diagnostics.
- `features` toggles lights, sound, Wi-Fi, sensors, and tip-over protection.
- `tests` runs the motor sweep, sound pulse, and battery voltage routines.
- `save`, `load`, `defaults`, and `reset` still manage stored settings and factory presets.

UART pin roles (`slave_tx` / `slave_rx`), PCA address, and every motor/lighting pin are now documented on the Control Hub, so use the web UI when rewiring or swapping hardware.

Settings survive power cycles via the on-board NVS/Preferences store.

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
- **Connectivity blink codes:** Wi-Fi and RC link each trigger their own chase pattern so you can tell which link dropped.
- **Sensor tinting:** CH5/CH6 (ultrasonic) modulate the headlight color from green (clear) to red (danger) as obstacles get closer.

Address, frequency, RGB channel assignments, and blink behaviors are all editable from the serial wizard so you can match whatever wiring layout you prefer.

## Wi-Fi control panel
_Note:_ To keep the core RC loop stable on low-power setups, networking is disabled (`TANKRC_ENABLE_NETWORK=0`) by default. Flip it to `1` once you’re ready for Wi-Fi/web control.

When the ESP32 joins your Wi-Fi (or exposes its fallback `sharc` access point), point a browser to `http://<device-ip>/` to open the TankRC Control Hub. The refreshed interface now focuses on feature switches, diagnostics, and pin ownership:
- Feature cards let you toggle lighting, sound, sensors, Wi-Fi, ultrasonic sensors, and tip-over handling without running `wizard features`.
- The dashboard surface-updates RC/Wi-Fi status, mode, and the same telemetry that previously animated the mock tank.
- Pin assignment cards display every GPIO/PCF entry per board, grouped under master/slave tabs, with hints about the owner, type (PWM, UART, lighting, etc.), and whether the expander is allowed. Cards validate input and push changes directly via `/api/config`.
- Download session logs (`/api/logs?format=csv`), back up the runtime configuration (JSON export/import), or telnet into the remote console (`telnet <ip> 2323`) to replay serial commands over Wi-Fi.
- Default fallback AP: **SSID** `sharc`, **password** `tankrc123`.

Changes saved through the web interface persist via NVS and automatically reconfigure the firmware.

## Multiple ESP targets
- **Master ESP**: Build/upload `TankRC_Master/TankRC_Master.ino` (or `pio run -e master`). This sketch now proxies the drive loop—motor drivers live on the slave, so the master binary is ~30% lighter and fits comfortably in flash with every feature enabled. Re-run the pin wizard after wiring the slave UART so the master can push pin assignments across.
- **Slave ESP**: Flash `TankRC_Slave/TankRC_Slave.ino` (or `pio run -e slave`). It boots the original drive controller/motor driver stack, listens for configuration + drive commands over Serial1 (default RX=16, TX=17), and streams battery telemetry back to the master. Keep the link crossed (master TX → slave RX and master RX ← slave TX) plus ground, and the master will automatically resend config data whenever you tweak pins from the wizard.
- Shared modules live at the repository root (`TankRC.h`, `config/`, etc.), so both sketches can `#include "../../TankRC.h"` and reuse the same codebase.

### Building
- **PlatformIO**: `pio run -e master -t upload` (default) or `pio run -e slave -t upload` once the slave sketch is implemented.
- **Arduino CLI/IDE**: Open the desired sketch folder (`TankRC_Master/` or `TankRC_Slave/`). With the helper script you can run `SKETCH=master ./scripts/build_and_flash.sh /dev/ttyUSB0` (or `SKETCH=slave ...`) to target a specific board.
  - The slave sketch reuses the master’s sources via symlinks. On platforms without symlink support, copy the shared folders from `TankRC_Master/` into `TankRC_Slave/` before building.
