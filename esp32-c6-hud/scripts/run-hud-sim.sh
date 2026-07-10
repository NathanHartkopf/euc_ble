#!/usr/bin/env bash
set -euo pipefail

if [[ ! -f /opt/homebrew/include/SDL2/SDL.h && ! -f /usr/local/include/SDL2/SDL.h ]]; then
  echo "SDL2 not found. Install it first:"
  echo "  brew install sdl2"
  exit 1
fi

cd "$(dirname "$0")/.."
export PATH="${HOME}/Library/Python/3.14/bin:${PATH}"

pio run -e hud-sim

BIN=".pio/build/hud-sim/program"
PORT="${SERIAL_PORT:-}"

if [[ -z "${PORT}" ]]; then
  PORT="$(ls /dev/cu.usbmodem* 2>/dev/null | head -1 || true)"
fi

ARGS=()
if [[ -n "${PORT}" ]]; then
  ARGS+=(--port "${PORT}")
  echo "Using Nano serial port: ${PORT}"
else
  echo "No /dev/cu.usbmodem* port found. Plug in the Nano or set SERIAL_PORT."
fi

echo "Launching HUD simulator (Nano serial telemetry)..."
echo "Close other serial monitors before connecting."
echo ""

exec "${BIN}" "${ARGS[@]}" "$@"
