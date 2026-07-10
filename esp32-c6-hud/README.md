# esp32-c6-hud

LVGL telemetry HUD for a **Nosfet Apex** on the **Waveshare ESP32-C6-LCD-1.47** (320×172 landscape). Also acts as a **BLE repeater** so phone apps (e.g. DarknessBot) can connect to the C6 instead of the wheel.

Full repo documentation: [../README.md](../README.md)

## What it does

- **BLE central** to the wheel (Veteran protocol on FFE0/FFE1, same as the Nano project)
- **BLE peripheral repeater** — advertises as `NF7266` (or the wheel name), proxies FFE1 notify/write
- **LVGL HUD** on the onboard ST7789 panel:
  - Large speed readout (mph) with a native 115px digit font
  - Vertical battery and temperature bars with clipped value labels
  - Voltage / current row at the bottom
  - Background color by speed: dark (normal), orange (>52 mph), red (>55 mph); speed text turns black on orange
  - Status line shows `WHEEL_DISPLAY_NAME` when connected, plus `+DarknessBot` when a proxy app is attached

## Desktop simulator

The simulator uses the **same** `src/hud_ui.cpp` and speed font as the ESP firmware. Preview UI changes on macOS before flashing hardware.

**Live mode** (default) — reads `HUD,spd,volt,amp,batt,tmp,chg` lines from the Nano over USB serial (`SERIAL_HUD_STREAM` in the Nano `config.h`).

**Mock mode** — animated demo telemetry (speed triangle wave 0→60→0 mph).

```bash
# Requires SDL2: brew install sdl2
./scripts/run-hud-sim.sh              # live Nano serial (auto-detects /dev/cu.usbmodem*)
./scripts/run-hud-sim.sh --mock         # demo data, no wheel/Nano needed
./scripts/run-hud-sim.sh --port /dev/cu.usbmodem21101
```

Close other serial monitors before connecting to the Nano in live mode.

## Flash ESP32-C6

```bash
cd esp32-c6-hud
pio run -e esp32-c6-hud -t upload
pio device monitor -b 115200
```

## Configuration (`include/config.h`)

| Setting | Purpose |
|---------|---------|
| `WHEEL_MAC_ADDRESS` | Direct-connect BLE MAC |
| `WHEEL_DISPLAY_NAME` | Friendly name shown on the HUD when connected (empty = use BLE name) |
| `REPEATER_DEVICE_NAME` | Default BLE peripheral name before the wheel is discovered |
| `WHEEL_DIRECT_CONNECT` | `1` = scan for configured MAC on boot |
| `HUD_IDLE_REFRESH_MS` | Redraw interval when telemetry is idle |

## PlatformIO environments

| Environment | Target |
|-------------|--------|
| `esp32-c6-hud` | Waveshare ESP32-C6-LCD-1.47 (production firmware) |
| `hud-sim` | macOS desktop preview (SDL2 + optional Nano serial) |

## Key files

| File | Purpose |
|------|---------|
| `src/hud_ui.cpp` | Shared HUD layout and styling (sim + ESP) |
| `src/euc_ble.cpp` | NimBLE central + repeater |
| `src/display_lvgl.cpp` | ST7789 display driver |
| `src/fonts/lv_font_speed_115.c` | 115px digit font for speed |
| `sim/main.cpp` | Simulator entry (mock or serial) |
| `include/config.h` | Wheel MAC, display name, BLE UUIDs |

## Architecture

```
Nosfet Apex  ◀──BLE central────  ESP32-C6 HUD  ──BLE peripheral──▶  DarknessBot / phone
                 (FFE1 notify)                      (FFE0/FFE1 proxy)

Nano 33 IoT  ──USB serial HUD lines──▶  Desktop hud-sim  (UI development only)
```
