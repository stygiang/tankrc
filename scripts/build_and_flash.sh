#!/usr/bin/env bash
set -euo pipefail

# Simple helper for Arduino IDE users (arduino-cli) with a PlatformIO fallback.
PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
cd "$PROJECT_DIR"

SKETCH="${SKETCH:-master}"
FQBN="esp32:esp32:esp32"
PORT="${1:-}"

case "$SKETCH" in
  master|Master)
    SKETCH_DIR="TankRC_Master"
    ;;
  slave|Slave)
    SKETCH_DIR="TankRC_Slave"
    ;;
  *)
    echo "Unknown sketch '$SKETCH' (expected 'master' or 'slave')." >&2
    exit 1
    ;;
esac

SKETCH_PATH="$PROJECT_DIR/$SKETCH_DIR"

if [[ ! -d "$SKETCH_PATH" ]]; then
  echo "Sketch directory '$SKETCH_PATH' not found." >&2
  exit 1
fi

if command -v arduino-cli >/dev/null 2>&1; then
  arduino-cli compile --fqbn "$FQBN" "$SKETCH_PATH"
  if [[ -n "$PORT" ]]; then
    arduino-cli upload --fqbn "$FQBN" -p "$PORT" "$SKETCH_PATH"
  else
    echo "Compile complete. Re-run with a serial port (e.g. SKETCH=slave ./scripts/build_and_flash.sh /dev/ttyUSB0) to upload."
  fi
elif command -v pio >/dev/null 2>&1; then
  ENV_NAME="$SKETCH"
  pio run -e "$ENV_NAME" --target upload
else
  echo "No known toolchain (pio or arduino-cli) found." >&2
  exit 1
fi
