# euc_ble

Four PlatformIO projects for electric-unicycle (EUC) telemetry and low-latency WiFi video:

- **Arduino Nano 33 IoT** — BLE telemetry collector + web UI (reference implementation)
- **ESP32-S3 CAM** — WiFi UDP video sender
- **ESP32-S3 LCD** — WiFi UDP video receiver
- **ESP32-C6 LCD** — LVGL telemetry HUD (BLE to Nosfet Apex)

---

## Table of contents

1. [Why this repo exists](#why-this-repo-exists)
2. [Repository layout](#repository-layout)
3. [Project 1: Arduino Nano 33 IoT — BLE telemetry](#project-1-arduino-nano-33-iot--ble-telemetry)
4. [Project 2: ESP32-CAM-S3 — video streamer](#project-2-esp32-cam-s3--video-streamer)
5. [Project 3: ESP32-S3 LCD — video receiver](#project-3-esp32-s3-lcd--video-receiver)
6. [Project 4: ESP32-C6 LCD — EUC telemetry HUD](#project-4-esp32-c6-lcd--euc-telemetry-hud)
7. [Shared video protocol](#shared-video-protocol)
8. [Hardware we used](#hardware-we-used)
9. [Tooling and prerequisites](#tooling-and-prerequisites)
10. [What we learned about the Nosfet Apex](#what-we-learned-about-the-nosfet-apex)
11. [NINA firmware update (WiFi + BLE concurrent)](#nina-firmware-update-wifi--ble-concurrent)
12. [Build, flash, and monitor](#build-flash-and-monitor)
13. [Troubleshooting](#troubleshooting)
14. [Current status and next steps](#current-status-and-next-steps)

---

## Why this repo exists

The goal is to read live data from a **Nosfet Apex** electric unicycle over Bluetooth Low Energy, and (separately) stream near-real-time camera video from an ESP32-CAM to a small LCD worn or mounted nearby.

The wheel was initially described as BLE device **NF7266** with GATT service **FFE0** and characteristic **FFE1** (the same HM-10-style UART service used by many EUC apps, including eucWatch's Ninebot module). Investigation showed the Apex is a **Nosfet** wheel built on **Veteran/LeaperKim** hardware and firmware, which also uses FFE0/FFE1 on the wire but with a different binary protocol (magic bytes `DC 5A 5C`, not Ninebot `55 AA`).

---

## Repository layout

```
euc_ble/
├── README.md                          ← this file
├── .gitignore
│
├── arduino-nano33iot-nf7266/          ← BLE central + Veteran parser + web UI
│   ├── platformio.ini
│   ├── include/
│   │   ├── config.h                   BLE UUIDs, hotspot, wheel MAC, display name, timings
│   │   ├── web_server.h               HTTP server + web UI (HTML/JS)
│   │   ├── ble_scan_store.h           BLE scan cache for API
│   │   ├── veteran_protocol.h         Frame reassembly + telemetry parser + HUD serial line
│   │   └── ninebot_protocol.h         Legacy Ninebot parser (unused)
│   └── src/
│       └── main.cpp                   Production firmware (BLE + hotspot + web UI)
│
├── esp32-cam-s3-streamer/             ← Camera → UDP JPEG sender
│   ├── platformio.ini
│   ├── include/config.h, camera_pins.h, video_protocol.h
│   └── src/main.cpp
│
├── esp32-s3-lcd-display/              ← UDP JPEG receiver → ST7789 LCD (S3 + PSRAM)
│   ├── platformio.ini
│   ├── include/config.h, display_setup.h, frame_receiver.h, video_protocol.h
│   └── src/main.cpp, frame_receiver.cpp
│
└── esp32-c6-hud/                      ← BLE Veteran HUD + repeater → LVGL on ST7789 LCD
    ├── README.md
    ├── platformio.ini                 esp32-c6-hud + hud-sim environments
    ├── scripts/run-hud-sim.sh         Desktop preview (SDL2 + Nano serial or mock)
    ├── include/
    │   ├── config.h                   Wheel MAC, display name, BLE UUIDs, LVGL config
    │   ├── veteran_protocol.h, euc_ble.h, hud_ui.h, hud_view.h
    │   └── lv_font_speed_115.h
    ├── src/
    │   ├── main.cpp, euc_ble.cpp, display_lvgl.cpp, hud_ui.cpp
    │   └── fonts/lv_font_speed_115.c
    └── sim/                           Desktop simulator (shared hud_ui.cpp)
```

---

## Project 1: Arduino Nano 33 IoT — BLE telemetry

### What it does

On boot the board:

1. Starts a **WiFi hotspot** (`EUC-NANO` / `eucnano1`, default IP **192.168.4.1**) so any phone or laptop can open the web UI without joining your home network.
2. Initializes **ArduinoBLE** as a central and **direct-connects** to the configured wheel MAC (`88:25:83:f6:21:0f` by default), or connects via BLE scan.
3. Subscribes to **FFE1** notifications and reassembles **Veteran** frames (`DC 5A 5C` …).
4. Serves a **web UI** on port **80**: speedometer (mph), live telemetry (imperial), BLE scanner, Connect/Scan buttons.
5. Optionally streams **HUD serial lines** for the desktop simulator when `SERIAL_HUD_STREAM` is enabled.
6. Auto-reconnects to the wheel on BLE disconnect.

### Architecture

```
                    ┌─────────────────────────────────────┐
  Phone / laptop    │  Nano 33 IoT (SAMD21 + NINA-W102)   │
  joins EUC-NANO ──▶│  Hotspot 192.168.4.1  :80  web UI   │
                    │  BLE central ──▶ Nosfet Apex (FFE1) │
                    │  USB serial HUD feed (optional)     │
                    └─────────────────────────────────────┘
```

After NINA **3.0.1**, WiFi and BLE run **concurrently** on the hotspot.

### Web UI

| Feature | Details |
|---------|---------|
| Hotspot | `EUC-NANO` / `eucnano1` → http://192.168.4.1/ |
| Speedometer | Large **mph** readout |
| Telemetry | Voltage, current, battery %, temp **°F**, trip/odo **mi** |
| Timing bar | Wheel Hz, web poll interval, data age, frames/poll |
| Scan | 12 s BLE discovery table |
| Connect | Direct connect to `WHEEL_MAC_ADDRESS` |

Poll interval: `WEB_REFRESH_MS` (default **250 ms**) in `config.h`.

When `SERIAL_HUD_STREAM` is `1`, each telemetry frame also prints a CSV line on USB serial for the [ESP32-C6 HUD simulator](esp32-c6-hud/README.md):

```
HUD,<speed_kmh>,<voltage_v>,<current_a>,<battery_pct>,<temp_c>,<charging>
```

### HTTP API

| Method | Path | Purpose |
|--------|------|---------|
| GET | `/` | Web page |
| GET | `/api/devices` | JSON: `devices[]`, `telemetry`, `wifi`, `connected` |
| POST | `/api/scan` | Start BLE scan |
| POST | `/api/connect` | Direct connect to configured MAC |

### Veteran BLE protocol (summary)

Reference: [eucplanet `docs/protocols/veteran.md`](https://github.com/eried/eucplanet/blob/main/docs/protocols/veteran.md)

| Item | Value |
|------|-------|
| GATT service | `0000ffe0-0000-1000-8000-00805f9b34fb` (short: `ffe0`) |
| GATT characteristic | `0000ffe1-0000-1000-8000-00805f9b34fb` (notify + write) |
| Frame magic | `DC 5A 5C` |
| Speed | i16 BE at offset 6, scale ÷10 → km/h |
| Voltage | u16 BE at offset 4, scale ÷100 → V |
| Current | i16 BE at offset 16, scale ÷10 → A |
| Temp | i16 BE at offset 18, scale ÷100 → °C |
| Battery % | Computed from voltage (Lynx/Apex family curve) |
| Nosfet Apex model id | 42 (36 cells, 151.2 V pack) |

Telemetry is **passive** — subscribe to FFE1 and the wheel pushes frames. No polling commands are required (unlike Ninebot).

Implementation: `include/veteran_protocol.h` (`FrameReassembler` + `parseRealtimeFrame`).

### PlatformIO environments

| Environment | Purpose | Source |
|-------------|---------|--------|
| `nano33iot` | **Production** — BLE + Veteran + hotspot + web UI | `src/main.cpp` |

### Configuration files

**`include/config.h`** — Hotspot SSID/password, wheel MAC, display name, web poll rate, BLE matching, HUD serial stream.

| Setting | Purpose |
|---------|---------|
| `WHEEL_MAC_ADDRESS` | Direct-connect BLE MAC |
| `WHEEL_DISPLAY_NAME` | Friendly name for web UI / HUD (empty = use BLE name) |
| `SERIAL_HUD_STREAM` | `1` = print `HUD,...` CSV lines on USB serial for desktop simulator |

**`include/veteran_protocol.h`** — Parser; no user config needed.

### Serial output (healthy boot)

```
NINA firmware: 3.0.1
NINA supports concurrent WiFi+BLE
Starting hotspot EUC-NANO
Hotspot ready @ 192.168.4.1
Direct connect scan for 88:25:83:f6:21:0f
Direct connect: MAC found
Subscribed to FFE1 — waiting for Veteran frames
Connected — streaming Veteran telemetry
spd=0.0 km/h  amp=0.0 A  volt=151.2 V  batt=85%  ...
```

---

## Project 2: ESP32-CAM-S3 — video streamer

### What it does

- Boots as WiFi **access point**: SSID `EUC-VIDEO`, password `eucvideo1`.
- Captures **QVGA (320×240) JPEG** from the onboard camera (~15 FPS target).
- Fragments each JPEG and **UDP-broadcasts** on port **5555** using the shared `video_protocol.h` format.

### PlatformIO

```ini
[env:esp32cam-s3]
platform = espressif32
board = esp32-s3-devkitc-1
```

Pins: Freenove ESP32-S3 WROOM camera profile in `include/camera_pins.h`.

**Status:** Created and builds; not yet field-tested in this session.

---

## Project 3: ESP32-S3 LCD — video receiver

### What it does

- Runs on **Waveshare ESP32-S3-LCD-1.47** (16 MB flash, 8 MB OPI PSRAM, dual-core @ 240 MHz).
- Connects as WiFi **station** to the camera AP (`EUC-VIDEO`).
- Listens for UDP port **5555**, reassembles JPEG fragments into **PSRAM frame buffers**.
- Decodes and displays on the onboard **1.47" ST7789** panel in landscape (**320×172** visible area).
- Uses a **dual-core pipeline**: core 0 receives UDP, core 1 decodes and draws — leaves headroom for future LVGL overlays.

### PlatformIO environments

| Environment | Board |
|-------------|-------|
| `esp32-s3-lcd` | Waveshare ESP32-S3-LCD-1.47 (**default / supported**) |

Uses GFX Library + JPEGDEC. PSRAM buffers and dual-core tasks are enabled on the S3 target.

**Status:** Built for S3; not yet field-tested.

---

## Project 4: ESP32-C6 LCD — EUC telemetry HUD + BLE repeater

See also: [esp32-c6-hud/README.md](esp32-c6-hud/README.md)

### What it does

- Runs on **Waveshare ESP32-C6-LCD-1.47** (4 MB flash, 512 KB SRAM, BLE 5).
- **BLE central** to the Nosfet Apex using the same Veteran protocol as the Nano project:
  - GATT service **FFE0**, notify characteristic **FFE1**
  - Frame magic `DC 5A 5C`, direct connect to configured wheel MAC
- **BLE peripheral repeater** so phone apps (e.g. DarknessBot) can connect to the C6 instead of the wheel:
  - Advertises as **`NF7266`** (or the wheel's actual name once discovered)
  - Exposes the same **FFE0 / FFE1** GATT service
  - **Notify passthrough** — wheel FFE1 bytes forwarded unchanged to the app
  - **Write passthrough** — app FFE1 writes forwarded unchanged to the wheel
- Renders an **LVGL HUD** on the onboard 1.47" ST7789 panel (landscape 320×172):
  - Large speed readout (mph) using a native **115px digit font**
  - Vertical **battery** and **temperature** bars with clipped value labels
  - Voltage / current row at the bottom
  - Background color by speed: dark (normal), orange (>52 mph), red (>55 mph)
  - Speed text turns **black** on the orange warning background
  - Status line shows **`WHEEL_DISPLAY_NAME`** when connected (instead of the BLE name), plus **`+DarknessBot`** when a proxy app is attached

### Desktop simulator

The simulator uses the **same** `src/hud_ui.cpp` as the ESP firmware. Preview UI changes on macOS before flashing hardware.

```bash
cd esp32-c6-hud
./scripts/run-hud-sim.sh              # live Nano serial (auto-detects /dev/cu.usbmodem*)
./scripts/run-hud-sim.sh --mock         # animated demo, no hardware needed
```

Requires **SDL2** (`brew install sdl2`). Live mode reads `HUD,...` CSV lines from the Nano (`SERIAL_HUD_STREAM` in Nano `config.h`).

### Architecture (split roles)

```
ESP32-S3 CAM  ──WiFi UDP video──▶  ESP32-S3 LCD  (live camera feed)

Nosfet Apex  ◀──BLE central────  ESP32-C6 HUD  ──BLE peripheral──▶  DarknessBot / phone
                 (FFE1 notify)                      (FFE0/FFE1 proxy)

Nano 33 IoT  ──USB serial HUD lines──▶  Desktop hud-sim  (UI development)
```

The C6 holds the only wheel BLE connection. Apps see the HUD advertising as the wheel and get the same GATT data stream.

### PlatformIO

```ini
[env:esp32-c6-hud]
platform = espressif32@55.3.39   ; pioarduino fork — required for C6 + Arduino 3.x
board = esp32-c6-devkitc-1

[env:hud-sim]
platform = native                ; macOS desktop preview (SDL2)
```

Uses **NimBLE-Arduino**, **LVGL 8.3**, and **GFX Library for Arduino**.

Wheel MAC, display name (`WHEEL_DISPLAY_NAME`), advertised name (`REPEATER_DEVICE_NAME`), and BLE UUIDs are in `include/config.h`.

**Status:** Simulator tested on macOS; ESP firmware built, not yet field-tested on hardware.

---

## Shared video protocol

Defined in `video_protocol.h` (identical in both ESP32 projects):

| Field | Value |
|-------|-------|
| Magic | `EVCV` (4 bytes) |
| UDP port | 5555 |
| Max JPEG size | 32 KB |
| Packet layout | Header + chunked payload with sequence/frame IDs |

The receiver crops QVGA 320×240 → 320×172 to match the physical panel.

---

## Hardware we used

| Device | Role |
|--------|------|
| **Arduino Nano 33 IoT** | BLE telemetry collector |
| **Nosfet Apex** | Target EUC (151.2 V, Veteran protocol) |
| **ESP32-S3 CAM** (Freenove) | Planned video sender |
| **ESP32-S3-LCD-1.47** (Waveshare) | Video receiver (8 MB PSRAM) |
| **ESP32-C6-LCD-1.47** (Waveshare) | BLE telemetry HUD (LVGL) |

---

## Tooling and prerequisites

### PlatformIO

```bash
pip install platformio
# binary typically at ~/Library/Python/3.14/bin/pio on macOS
export PATH="$HOME/Library/Python/3.14/bin:$PATH"
```

### Nano 33 IoT — extra tools (in `.tools/`)

| Tool | Version | Purpose |
|------|---------|---------|
| `arduino-fwuploader` | 2.4.1 | Flash **NINA-W102** module firmware |

Downloaded from [arduino-fwuploader releases](https://github.com/arduino/arduino-fwuploader/releases).

### Libraries (nano33iot env)

| Library | Version | Notes |
|---------|---------|-------|
| ArduinoBLE | ^2.0.0 | Required for concurrent WiFi+BLE |
| WiFiNINA | ^2.0.0 | Required for concurrent WiFi+BLE |

---

## What we learned about the Nosfet Apex

### BLE name

| Tool | What it shows |
|------|---------------|
| **iPhone** (DarknessBot, nRF Connect) | `NF7266` |
| **Mac bleak** | A device named `EUC` (Apple mfg ID 76) — **not the wheel** |
| **Nano serial scan** | Many anonymous `adv` lines; `NF7266` not yet seen when wheel was off or phone-connected |

The Mac `EUC` device persists even with the wheel powered off — it is an unrelated Apple peripheral. Do not connect to it.

### Protocol

- **Not Ninebot** (`55 AA` commands) — initial `ninebot_protocol.h` was replaced by `veteran_protocol.h`.
- **Veteran/LeaperKim** wire format on **FFE0/FFE1** — same GATT UUIDs as Begode/Ninebot HM-10 bridge, different payload.
- Nosfet Apex is model **42** in the Veteran firmware version field (36-cell Lynx-family pack).

### Connection tips

1. Power the wheel on and keep it within a few feet of the Nano.
2. **Disconnect the phone app** — most wheels allow only one BLE central at a time.
3. Reset the Nano and watch serial for `name='NF7266'` in `adv` lines.
4. If name never appears but FFE0 does, the firmware will still connect via service UUID match.

---

## NINA firmware update (WiFi + BLE concurrent)

### Problem

On NINA firmware **1.4.8** (factory default on many boards), the NINA module could run **WiFi or BLE, not both**. Running both caused the sketch to hang with no serial output.

### Solution

Update NINA to **3.0.1** and use library versions **2.0.0+**.

### Steps we performed

```bash
cd arduino-nano33iot-nf7266/.tools

# 1. Download arduino-fwuploader (macOS ARM64)
curl -fsSL -o arduino-fwuploader.tar.gz \
  "https://github.com/arduino/arduino-fwuploader/releases/download/2.4.1/arduino-fwuploader_2.4.1_macOS_ARM64.tar.gz"
tar -xzf arduino-fwuploader.tar.gz

# 2. Check current NINA version
./arduino-fwuploader firmware get-version \
  -b arduino:samd:nano_33_iot \
  -a /dev/cu.usbmodem21101
# Was: 1.4.8

# 3. Flash NINA 3.0.1 (~65 seconds)
./arduino-fwuploader firmware flash \
  -b arduino:samd:nano_33_iot \
  -a /dev/cu.usbmodem21101 \
  -m NINA@3.0.1

# 4. Verify
./arduino-fwuploader firmware get-version \
  -b arduino:samd:nano_33_iot \
  -a /dev/cu.usbmodem21101
# Now: 3.0.1
```

**Note:** The flasher uploads a small stub sketch to the SAMD chip first, then flashes the NINA module. Re-flash `nano33iot` over USB afterward.

### After NINA 3.0.1

- `platformio.ini` uses `ArduinoBLE @ ^2.0.0` and `WiFiNINA @ ^2.0.0`.
- `main.cpp` detects `firmware >= 3.0.1` and keeps WiFi hotspot + web UI running while BLE connects.
- Serial shows both hotspot ready and direct connect / scan activity at the same time.

---

## Build, flash, and monitor

### Nano 33 IoT — USB flash

```bash
cd arduino-nano33iot-nf7266
pio run -e nano33iot -t upload
# or specify port:
pio run -e nano33iot -t upload --upload-port /dev/cu.usbmodem21101
```

Board must be in runtime mode (USB PID **8057**). Bootloader is PID **0057** if upload fails — double-tap reset.

### ESP32-C6 HUD — USB flash

```bash
cd esp32-c6-hud
pio run -e esp32-c6-hud -t upload
pio device monitor -b 115200
```

### ESP32-C6 HUD — desktop simulator

```bash
cd esp32-c6-hud
brew install sdl2   # one-time
./scripts/run-hud-sim.sh --mock     # demo UI without hardware
./scripts/run-hud-sim.sh            # live Nano serial feed
```

### Serial monitor (Nano)

```bash
pio device monitor -b 115200
# or
python3 -c "
import serial, time, sys
s = serial.Serial('/dev/cu.usbmodem21101', 115200)
s.dtr = True
while True:
    sys.stdout.write(s.read(256).decode('utf-8', errors='replace'))
    sys.stdout.flush()
"
```

### ESP32 video projects

```bash
# Camera sender
cd esp32-cam-s3-streamer
pio run -e esp32cam-s3 -t upload

# LCD video receiver (S3)
cd esp32-s3-lcd-display
pio run -e esp32-s3-lcd -t upload
```

---

## Troubleshooting

### Nano uploads to wrong USB device

If multiple serial devices are present (e.g. Bose speaker), specify the port explicitly or unplug other devices. Nano 33 IoT shows as `Arduino NANO 33 IoT` in `pio device list`.

### `pio device monitor` fails in some shells

Use pyserial directly with `dtr=True` (see Serial monitor above).

### BLE scan runs but never finds NF7266

- Wheel off, out of range, or phone app connected.
- Enable `BLE_DEBUG_SCAN 1` in `config.h` and look for `name='NF7266'` or `svcs=ffe0`.
- Ignore Mac `bleak` device named `EUC` — it is not the wheel.

### Connected but no telemetry on web UI

- Veteran frames arrive in multiple 20-byte BLE notifications; the firmware uses an event handler to reassemble them.
- Confirm Serial shows `spd=…` lines.
- Web poll is 250 ms by default; check the timing bar for wheel Hz vs data age.

### Hotspot not reachable

- Confirm you joined `EUC-NANO` / `eucnano1` and open http://192.168.4.1/
- Check serial for `Hotspot ready @ 192.168.4.1`

### HUD simulator shows no telemetry

- Close other serial monitors (only one client can open the Nano USB port).
- Confirm Nano has `SERIAL_HUD_STREAM 1` in `config.h` and is flashed.
- Use `--mock` to verify the UI without hardware: `./scripts/run-hud-sim.sh --mock`

### WiFi + BLE hang (pre-3.0.1 NINA)

Update NINA to 3.0.1 (see above). On old firmware the sketch falls back to a 12 s boot OTA window then BLE-only.

---

## Current status and next steps

| Component | Status |
|-----------|--------|
| Nano BLE + Veteran parser | **Working** — telemetry on Serial and web UI |
| Nano HUD serial stream | **Working** — feeds desktop HUD simulator |
| WiFi hotspot + web UI | **Working** — `EUC-NANO` @ 192.168.4.1 |
| Direct BLE connect (MAC) | **Working** — `88:25:83:f6:21:0f` |
| NINA firmware 3.0.1 | **Done** |
| ESP32-C6 HUD simulator | **Working** — mock + live Nano serial |
| ESP32-C6 HUD firmware | Built, not field-tested on hardware |
| ESP32-CAM streamer | Built, not field-tested |
| ESP32-S3 LCD receiver | Built for S3 + PSRAM, not field-tested |

### Recommended next steps

1. Flash and bench-test the ESP32-C6 HUD on hardware.
2. Flash and bench-test the ESP32 video pair.
3. Re-add OTA upload for the Nano if desired (currently USB-only).

---

## References

- [eucplanet Veteran protocol](https://github.com/eried/eucplanet/blob/main/docs/protocols/veteran.md)
- [eucWatch Ninebot module](https://github.com/enaon/eucWatch) (original FFE0/FFE1 reference; not used for Apex)
- [Arduino NINA concurrent WiFi+BLE](https://blog.arduino.cc/2026/03/02/you-can-now-use-wi-fi-and-bluetooth-le-simultaneously-on-arduino-nina-based-boards-heres-how/)
- [arduino-fwuploader](https://github.com/arduino/arduino-fwuploader)
