# euc_ble

Three PlatformIO projects for electric-unicycle (EUC) telemetry and low-latency WiFi video. Built on an Arduino Nano 33 IoT (BLE telemetry + OTA), an ESP32-CAM-S3 (camera sender), and an ESP32-C3/C6 with a 1.47" ST7789 LCD (video receiver).

---

## Table of contents

1. [Why this repo exists](#why-this-repo-exists)
2. [Repository layout](#repository-layout)
3. [Project 1: Arduino Nano 33 IoT — BLE telemetry](#project-1-arduino-nano-33-iot--ble-telemetry)
4. [Project 2: ESP32-CAM-S3 — video streamer](#project-2-esp32-cam-s3--video-streamer)
5. [Project 3: ESP32-C3/C6 LCD — video receiver](#project-3-esp32-c3c6-lcd--video-receiver)
6. [Shared video protocol](#shared-video-protocol)
7. [Hardware we used](#hardware-we-used)
8. [Tooling and prerequisites](#tooling-and-prerequisites)
9. [What we learned about the Nosfet Apex](#what-we-learned-about-the-nosfet-apex)
10. [NINA firmware update (WiFi + BLE concurrent)](#nina-firmware-update-wifi--ble-concurrent)
11. [Build, flash, and OTA](#build-flash-and-ota)
12. [Troubleshooting](#troubleshooting)
13. [Current status and next steps](#current-status-and-next-steps)

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
├── arduino-nano33iot-nf7266/          ← BLE central + Veteran parser + WiFi OTA
│   ├── platformio.ini
│   ├── include/
│   │   ├── config.h                   BLE UUIDs, hotspot, wheel MAC, timings
│   │   ├── web_server.h               HTTP server + web UI (HTML/JS)
│   │   ├── ble_scan_store.h           BLE scan cache for API
│   │   ├── veteran_protocol.h         Frame reassembly + telemetry parser
│   │   ├── ninebot_protocol.h         Legacy Ninebot parser (unused)
│   │   ├── wifi_secrets.h.example     Template for home WiFi credentials
│   │   └── wifi_secrets.h             Local secrets (gitignored)
│   ├── src/
│   │   ├── main.cpp                   Production firmware (BLE + OTA)
│   │   └── ota/ota_main.cpp           WiFi-only OTA bootstrap sketch
│   └── .tools/                        arduino-fwuploader, arduinoOTA binaries
│
├── esp32-cam-s3-streamer/             ← Camera → UDP JPEG sender
│   ├── platformio.ini
│   ├── include/config.h, camera_pins.h, video_protocol.h
│   └── src/main.cpp
│
└── esp32-c3-lcd-display/              ← UDP JPEG receiver → ST7789 LCD
    ├── platformio.ini
    ├── include/config.h, display_setup.h, frame_receiver.h, video_protocol.h
    └── src/main.cpp, frame_receiver.cpp
```

---

## Project 1: Arduino Nano 33 IoT — BLE telemetry

### What it does

On boot the board:

1. Starts a **WiFi hotspot** (`EUC-NANO` / `eucnano1`, default IP **192.168.4.1**) so any phone or laptop can open the web UI without joining your home network.
2. Initializes **ArduinoBLE** as a central and **direct-connects** to the configured wheel MAC (`88:25:83:f6:21:0f` by default), or connects via BLE scan.
3. Subscribes to **FFE1** notifications and reassembles **Veteran** frames (`DC 5A 5C` …).
4. Serves a **web UI** on port **80**: speedometer (mph), live telemetry (imperial), BLE scanner, Connect/Scan buttons.
5. **Switch to WiFi for OTA** button disconnects the hotspot, joins your home WiFi, and enables **ArduinoOTA** on port **65280** (NINA cannot run AP + STA simultaneously).
6. Auto-reconnects to the wheel on BLE disconnect.

### Architecture

```
                    ┌─────────────────────────────────────┐
  Phone / laptop    │  Nano 33 IoT (SAMD21 + NINA-W102)   │
  joins EUC-NANO ──▶│  Hotspot 192.168.4.1  :80  web UI   │
                    │  BLE central ──▶ Nosfet Apex (FFE1) │
                    └──────────────┬──────────────────────┘
                                   │ "Switch to WiFi for OTA"
                                   ▼
                    ┌─────────────────────────────────────┐
                    │  Home router (e.g. WiHelloThere)    │
                    │  ArduinoOTA :65280 ◀── PC upload  │
                    └─────────────────────────────────────┘
```

After NINA **3.0.1**, WiFi (either AP or STA) and BLE run **concurrently**. The board is in **one WiFi mode at a time** — hotspot for field use, home WiFi for OTA.

### Web UI

| Feature | Details |
|---------|---------|
| Hotspot | `EUC-NANO` / `eucnano1` → http://192.168.4.1/ |
| Speedometer | Large **mph** readout |
| Telemetry | Voltage, current, battery %, temp **°F**, trip/odo **mi** |
| Timing bar | Wheel Hz, web poll interval, data age, frames/poll |
| Scan | 12 s BLE discovery table |
| Connect | Direct connect to `WHEEL_MAC_ADDRESS` |
| WiFi toggle | **Switch to WiFi for OTA** / **Back to hotspot** |

Poll interval: `WEB_REFRESH_MS` (default **250 ms**) in `config.h`.

### HTTP API

| Method | Path | Purpose |
|--------|------|---------|
| GET | `/` | Web page |
| GET | `/api/devices` | JSON: `devices[]`, `telemetry`, `wifi`, `connected` |
| POST | `/api/scan` | Start BLE scan |
| POST | `/api/connect` | Direct connect to configured MAC |
| POST | `/api/wifi/ota` | Switch to home WiFi for OTA |
| POST | `/api/wifi/ap` | Switch back to hotspot |

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
| `nano33iot` | **Production** — BLE + Veteran + OTA | `src/main.cpp` |
| `nano33iot-ota` | **Bootstrap** — WiFi + OTA only (recovery) | `src/ota/ota_main.cpp` |

### Configuration files

**`include/config.h`** — Hotspot SSID/password, wheel MAC, web poll rate, BLE matching.

**`include/wifi_secrets.h`** — Home WiFi credentials for OTA (copy from `wifi_secrets.h.example`):

```cpp
#define SECRET_SSID "your-home-ssid"
#define SECRET_PASS "your-home-password"
#define OTA_UPLOAD_PASSWORD "password"
```

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

## Project 3: ESP32-C3/C6 LCD — video receiver

### What it does

- Connects as WiFi **station** to the camera AP (`EUC-VIDEO`).
- Listens for UDP port **5555**, reassembles JPEG fragments.
- Decodes and displays on a **Waveshare 1.47" ST7789** panel in landscape (**320×172** visible area).

### PlatformIO environments

| Environment | Board |
|-------------|-------|
| `esp32-c3-lcd` | Waveshare ESP32-C3 LCD 1.47" (default) |
| `esp32-c6-lcd` | Waveshare ESP32-C6 LCD 1.47" |

Uses GFX Library + JPEGDEC.

**Status:** Created and builds; not yet field-tested in this session.

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
| **ESP32-C3 + 1.47" LCD** (Waveshare) | Planned video receiver |

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
| `arduinoOTA` | 1.4.1 | Upload SAMD sketch over WiFi |

Downloaded from [arduino-fwuploader releases](https://github.com/arduino/arduino-fwuploader/releases) and [arduinoOTA releases](https://github.com/arduino/arduinoOTA/releases).

### Libraries (nano33iot env)

| Library | Version | Notes |
|---------|---------|-------|
| ArduinoBLE | ^2.0.0 | Required for concurrent WiFi+BLE |
| WiFiNINA | ^2.0.0 | Required for concurrent WiFi+BLE |
| ArduinoOTA | ^1.1.1 | jandrassy fork for SAMD InternalStorage |

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
- `main.cpp` detects `firmware >= 3.0.1` and keeps WiFi + OTA running while BLE scans.
- Serial shows both `ArduinoOTA ready @ …` and `Scanning for NF7266 / FFE0…` at the same time.

---

## Build, flash, and OTA

### Nano 33 IoT — USB flash (first time or recovery)

```bash
cd arduino-nano33iot-nf7266
pio run -e nano33iot -t upload
# or specify port:
pio run -e nano33iot -t upload --upload-port /dev/cu.usbmodem21101
```

Board must be in runtime mode (USB PID **8057**). Bootloader is PID **0057** if upload fails — double-tap reset.

### Nano 33 IoT — OTA flash (after switching to home WiFi)

1. Join hotspot `EUC-NANO`, open http://192.168.4.1/
2. Tap **Switch to WiFi for OTA**
3. On your Mac (on home WiFi):

```bash
cd arduino-nano33iot-nf7266
pio run -e nano33iot

.tools/arduinoOTA_osx_darwin_arm64/arduinoOTA \
  -address 192.168.4.26 \
  -port 65280 \
  -username arduino \
  -password password \
  -sketch .pio/build/nano33iot/firmware.bin \
  -upload /sketch \
  -t 120 \
  -b
```

**Verified 2026-07-10:** `Sketch uploaded successfully` after web UI WiFi switch.

After reboot the board returns to **hotspot mode**. Use **Back to hotspot** in the UI to switch manually without rebooting.

| OTA setting | Value |
|-------------|-------|
| IP | `192.168.4.26` (DHCP; may change) |
| Port | `65280` |
| Hostname | `nano33iot-nf7266` |
| Password | `password` (set in `wifi_secrets.h`) |

### Nano 33 IoT — OTA-only bootstrap (brick recovery)

If the main sketch is broken but you need network access:

```bash
pio run -e nano33iot-ota -t upload
```

This flashes `src/ota/ota_main.cpp` — WiFi + ArduinoOTA only, no BLE.

### Serial monitor

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

# LCD receiver (C3)
cd esp32-c3-lcd-display
pio run -e esp32-c3-lcd -t upload
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

### Hotspot vs home WiFi

- NINA **cannot** run AP and STA at the same time.
- Use the web UI **Switch to WiFi for OTA** before uploading from your PC.
- OTA requires your computer on the **same home WiFi** as the Nano (not on `EUC-NANO`).

### OTA returns Unauthorized

Password must match `OTA_UPLOAD_PASSWORD` in `wifi_secrets.h` (default `password`).

### OTA upload succeeds but sketch doesn't run

Close any serial monitor before OTA — an immediate USB reset after upload can interrupt flash apply on SAMD.

### WiFi + BLE hang (pre-3.0.1 NINA)

Update NINA to 3.0.1 (see above). On old firmware the sketch falls back to a 12 s boot OTA window then BLE-only.

---

## Current status and next steps

| Component | Status |
|-----------|--------|
| Nano BLE + Veteran parser | **Working** — telemetry on Serial and web UI |
| WiFi hotspot + web UI | **Working** — `EUC-NANO` @ 192.168.4.1 |
| WiFi mode switch + OTA | **Tested** — web button → home WiFi → OTA upload |
| Direct BLE connect (MAC) | **Working** — `88:25:83:f6:21:0f` |
| NINA firmware 3.0.1 | **Done** |
| ESP32-CAM streamer | Built, not field-tested |
| ESP32-C3 LCD receiver | Built, not field-tested |

### Recommended next steps

1. Flash and bench-test the ESP32 video pair.
2. Add optional mDNS for OTA hostname instead of hard-coded IP.
3. Persist last WiFi mode across reboot if desired (currently always boots to hotspot).

---

## References

- [eucplanet Veteran protocol](https://github.com/eried/eucplanet/blob/main/docs/protocols/veteran.md)
- [eucWatch Ninebot module](https://github.com/enaon/eucWatch) (original FFE0/FFE1 reference; not used for Apex)
- [Arduino NINA concurrent WiFi+BLE](https://blog.arduino.cc/2026/03/02/you-can-now-use-wi-fi-and-bluetooth-le-simultaneously-on-arduino-nina-based-boards-heres-how/)
- [arduino-fwuploader](https://github.com/arduino/arduino-fwuploader)
- [arduinoOTA](https://github.com/arduino/arduinoOTA)
