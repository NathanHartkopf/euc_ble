# arduino-nano33iot-nf7266

BLE telemetry bridge for a **Nosfet Apex** (Veteran protocol) on **FFE0/FFE1**, with a **built-in web UI** and **WiFi hotspot** on the Arduino Nano 33 IoT.

Full repo documentation: [../README.md](../README.md)

## Quick access

| Mode | How to connect | Web UI |
|------|----------------|--------|
| **Hotspot (default)** | WiFi `EUC-NANO` / `eucnano1` | http://192.168.4.1/ |
| **Home WiFi (OTA)** | Use **Switch to WiFi for OTA** in the web UI, then join your home network | http://\<dhcp-ip\>/ (e.g. `192.168.4.26`) |

The NINA module **cannot** run hotspot and home WiFi at the same time. Use the web UI button to switch modes.

## Web UI

- **Speedometer** — large mph display (imperial units throughout)
- **Live telemetry** — voltage, current, battery %, temp (°F), trip/odometer (mi), charging
- **Timing stats** — wheel frame rate vs web poll rate and data age
- **Scan** — 12 s BLE discovery, device table with target badges
- **Connect** — direct connect to configured wheel MAC (`WHEEL_MAC_ADDRESS` in `config.h`)
- **Switch to WiFi for OTA** / **Back to hotspot** — toggle WiFi mode

### API

| Method | Path | Purpose |
|--------|------|---------|
| GET | `/` | Web page |
| GET | `/api/devices` | JSON: devices, telemetry, WiFi mode, timing |
| POST | `/api/scan` | Start BLE scan |
| POST | `/api/connect` | Direct connect to `WHEEL_MAC_ADDRESS` |
| POST | `/api/wifi/ota` | Switch to home WiFi for OTA |
| POST | `/api/wifi/ap` | Switch back to hotspot |

## Setup

```bash
# 1. WiFi credentials (home network — for OTA only)
cp include/wifi_secrets.h.example include/wifi_secrets.h
# edit SECRET_SSID / SECRET_PASS

# 2. Optional: set your wheel's BLE MAC in include/config.h
#    #define WHEEL_MAC_ADDRESS "88:25:83:f6:21:0f"

# 3. Build and USB flash
pio run -e nano33iot -t upload

# 4. Join hotspot EUC-NANO / eucnano1 → http://192.168.4.1/
```

## OTA upload

1. Open http://192.168.4.1/ on a device joined to `EUC-NANO`
2. Tap **Switch to WiFi for OTA** and confirm
3. On your computer (on the **same home WiFi**), run:

```bash
pio run -e nano33iot
.tools/arduinoOTA_osx_darwin_arm64/arduinoOTA \
  -address 192.168.4.26 -port 65280 \
  -username arduino -password password \
  -sketch .pio/build/nano33iot/firmware.bin \
  -upload /sketch -t 120 -b
```

Install OTA tools into `.tools/` — see [main README](../README.md#tooling-and-prerequisites).

After OTA reboot the board returns to **hotspot mode** by default.

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
| `src/main.cpp` | BLE, WiFi modes, web server loop |
| `include/web_server.h` | HTTP server, HTML/JS UI |
| `include/veteran_protocol.h` | `DC 5A 5C` frame parser |
| `include/ble_scan_store.h` | Scan result cache for web API |
| `include/config.h` | UUIDs, hotspot SSID, wheel MAC, timings |
| `include/wifi_secrets.h` | Home WiFi + OTA password (not committed) |
| `src/ota/ota_main.cpp` | Recovery sketch (OTA only) |

## Wheel matching

- **Direct connect:** `WHEEL_MAC_ADDRESS` in `config.h` (boot + Connect button)
- **Scan auto-connect:** FFE0 advertised, or name **NF7266** / **NOSFET** / **APEX** (not bare **EUC** — that is often an unrelated Apple beacon)

iPhone apps show **`NF7266`** for the Nosfet Apex.
