#!/usr/bin/env bash
set -euo pipefail

# Simple helper for Arduino IDE users (arduino-cli) with a PlatformIO fallback.
PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
cd "$PROJECT_DIR"

FQBN="esp32:esp32:esp32"
PORT="${1:-}"

if command -v arduino-cli >/dev/null 2>&1; then
  arduino-cli compile --fqbn "$FQBN" .
  if [[ -n "$PORT" ]]; then
    arduino-cli upload --fqbn "$FQBN" -p "$PORT" .
  else
    echo "Compile complete. Re-run with a serial port (e.g. ./scripts/build_and_flash.sh /dev/ttyUSB0) to upload."
  fi
elif command -v pio >/dev/null 2>&1; then
  pio run --target upload
else
  echo "No known toolchain (pio or arduino-cli) found." >&2
  exit 1
fi
