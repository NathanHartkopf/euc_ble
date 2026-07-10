# arduino-nano33iot-nf7266

BLE telemetry bridge for a **Nosfet Apex** (Veteran protocol) on **FFE0/FFE1**, with a **built-in web UI** and **WiFi hotspot** on the Arduino Nano 33 IoT.

Full repo documentation: [../README.md](../README.md)

## Quick access

| Mode | How to connect | Web UI |
|------|----------------|--------|
| **Hotspot (default)** | WiFi `EUC-NANO` / `eucnano1` | http://192.168.4.1/ |

The board boots into hotspot mode and direct-connects to the configured wheel MAC.

## Web UI

- **Speedometer** — large mph display (imperial units throughout)
- **Live telemetry** — voltage, current, battery %, temp (°F), trip/odometer (mi), charging
- **Timing stats** — wheel frame rate vs web poll rate and data age
- **Scan** — 12 s BLE discovery, device table with target badges
- **Connect** — direct connect to configured wheel MAC (`WHEEL_MAC_ADDRESS` in `config.h`)

### API

| Method | Path | Purpose |
|--------|------|---------|
| GET | `/` | Web page |
| GET | `/api/devices` | JSON: devices, telemetry, WiFi mode, timing |
| POST | `/api/scan` | Start BLE scan |
| POST | `/api/connect` | Direct connect to `WHEEL_MAC_ADDRESS` |

## HUD simulator feed

When `SERIAL_HUD_STREAM` is `1` in `include/config.h`, the Nano prints one CSV line per telemetry frame on USB serial:

```
HUD,<speed_kmh>,<voltage_v>,<current_a>,<battery_pct>,<temp_c>,<charging>
```

The [ESP32-C6 HUD simulator](../esp32-c6-hud/README.md) reads these lines for live UI development on macOS. Close other serial monitors before connecting.

## Setup

```bash
# 1. Optional: set your wheel's BLE MAC and display name in include/config.h
#    #define WHEEL_MAC_ADDRESS "88:25:83:f6:21:0f"
#    #define WHEEL_DISPLAY_NAME "Apex"

# 2. Build and USB flash
pio run -e nano33iot -t upload

# 3. Join hotspot EUC-NANO / eucnano1 → http://192.168.4.1/
```

## NINA firmware (one-time, WiFi + BLE together)

Requires **NINA 3.0.1** + ArduinoBLE 2.x + WiFiNINA 2.x.

```bash
.tools/arduino-fwuploader firmware flash \
  -b arduino:samd:nano_33_iot \
  -a /dev/cu.usbmodem21101 \
  -m NINA@3.0.1
```

## Key files

| File | Purpose |
|------|---------|
| `src/main.cpp` | BLE, WiFi hotspot, web server loop |
| `include/web_server.h` | HTTP server, HTML/JS UI |
| `include/veteran_protocol.h` | `DC 5A 5C` frame parser + HUD serial line |
| `include/ble_scan_store.h` | Scan result cache for web API |
| `include/config.h` | UUIDs, hotspot SSID, wheel MAC, display name, timings |

## Wheel matching

- **Direct connect:** `WHEEL_MAC_ADDRESS` in `config.h` (boot + Connect button)
- **Scan auto-connect:** FFE0 advertised, or name **NF7266** / **NOSFET** / **APEX** (not bare **EUC** — that is often an unrelated Apple beacon)
- **Display name:** `WHEEL_DISPLAY_NAME` overrides the BLE name in the web UI and HUD simulator

iPhone apps show **`NF7266`** for the Nosfet Apex.
