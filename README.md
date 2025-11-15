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
Open a serial monitor at 115200 baud and type `help` to launch the interactive wizard. You can:
- Reassign any motor/feature pins without recompiling (`wizard pins`), then save them to flash.
- Enable/disable feature modules (`wizard features`), useful when hardware isn't installed yet.
- Run the bundled test suite (`wizard test`) to sweep the tank drive, blink the light bar, pulse the speaker, and read battery voltage before heading into the field.
- Configure the UART bridge to your drive slave (`slave_tx` / `slave_rx` tokens or through the pin wizard). By default the master uses TX=17, RX=16; cross them to the slave ESP32 and share ground so the master can stream drive commands + configuration downstream.

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
- **Connectivity blink codes:** Wi-Fi and RC link each trigger their own chase pattern so you can tell which link dropped.
- **Sensor tinting:** CH5/CH6 (ultrasonic) modulate the headlight color from green (clear) to red (danger) as obstacles get closer.

Address, frequency, RGB channel assignments, and blink behaviors are all editable from the serial wizard so you can match whatever wiring layout you prefer.

## Wi-Fi control panel
_Note:_ To keep the core RC loop stable on low-power setups, the firmware ships with networking disabled (`TANKRC_ENABLE_NETWORK=0`). Flip it to `1` once you’re ready for Wi-Fi/web control.

Once the ESP32 joins your Wi-Fi (or exposes its fallback `TankRC-Setup` access point), open `http://<device-ip>/` to launch the TankRC Control Hub. The web UI mirrors the serial wizard, adds live telemetry, a styled mock RC model, and manual overrides (hazard/lighting) without leaving your browser:
- Update Wi-Fi credentials, PCA9685 address/frequency, and feature toggles in one place.
- Watch real-time steering/throttle/ultrasonic data animate the mock tank.
- Trigger hazard flashers or force lighting states directly from the UI—useful when the radio is powered down.
- Default AP credentials: **SSID** `TankRC-Setup`, **password** `tankrc123`.
- Pull NTP time for accurate timestamps and download session logs (`/api/logs?format=csv`) for tuning or debugging.
- Back up or restore the entire runtime configuration via the dashboard (JSON export/import) to clone settings across vehicles.
- Telnet into the remote console (`telnet <ip> 2323`) to run the exact same serial commands over Wi-Fi (requires `TANKRC_ENABLE_NETWORK=1`).

Changes saved through the web interface persist via NVS and automatically reconfigure the firmware.

## Multiple ESP targets
- **Master ESP**: Build/upload `TankRC_Master/TankRC_Master.ino` (or `pio run -e master`). This sketch now proxies the drive loop—motor drivers live on the slave, so the master binary is ~30% lighter and fits comfortably in flash with every feature enabled. Re-run the pin wizard after wiring the slave UART so the master can push pin assignments across.
- **Slave ESP**: Flash `TankRC_Slave/TankRC_Slave.ino` (or `pio run -e slave`). It boots the original drive controller/motor driver stack, listens for configuration + drive commands over Serial1 (default RX=16, TX=17), and streams battery telemetry back to the master. Keep the link crossed (master TX → slave RX and master RX ← slave TX) plus ground, and the master will automatically resend config data whenever you tweak pins from the wizard.
- Shared modules live at the repository root (`TankRC.h`, `config/`, etc.), so both sketches can `#include "../../TankRC.h"` and reuse the same codebase.

### Building
- **PlatformIO**: `pio run -e master -t upload` (default) or `pio run -e slave -t upload` once the slave sketch is implemented.
- **Arduino CLI/IDE**: Open the desired sketch folder (`TankRC_Master/` or `TankRC_Slave/`). With the helper script you can run `SKETCH=master ./scripts/build_and_flash.sh /dev/ttyUSB0` (or `SKETCH=slave ...`) to target a specific board.
  - The slave sketch reuses the master’s sources via symlinks. On platforms without symlink support, copy the shared folders from `TankRC_Master/` into `TankRC_Slave/` before building.
