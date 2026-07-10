#pragma once

// NOSFET Apex uses the Veteran/LeaperKim wire protocol on HM-10 style GATT.
// Advertising names vary ("EUC", serial, custom); match primarily on FFE0.
#define EUC_SERVICE_UUID "0000ffe0-0000-1000-8000-00805f9b34fb"
#define EUC_TELEMETRY_UUID "0000ffe1-0000-1000-8000-00805f9b34fb"
#define EUC_SERVICE_UUID_SHORT "ffe0"

// Name hints — iPhone/DarknessBot report "NF7266" for NOSFET Apex.
#define EUC_NAME_HINT_COUNT 4
static const char *const EUC_NAME_HINTS[EUC_NAME_HINT_COUNT] = {
    "NF7266",
    "NOSFET",
    "APEX",
    "EUC",
};

// One-line CSV telemetry for the desktop HUD simulator (HUD,spd,volt,amp,batt,tmp,chg).
#define SERIAL_HUD_STREAM 1

// Set to 1 to print every BLE advertisement while scanning.
#if SERIAL_HUD_STREAM
#define BLE_DEBUG_SCAN 0
#else
#define BLE_DEBUG_SCAN 1
#endif

// Set to 1 to print raw FFE1 notification bytes (hex).
#define BLE_DEBUG_NOTIFY 0

// Reconnect delay after disconnect (ms).
#define EUC_RECONNECT_DELAY_MS 1500

// Web UI + BLE discovery scan.
#define HTTP_PORT 80
#define WEB_REFRESH_MS 250
#define BLE_SCAN_MAX_DEVICES 20
#define BLE_SCAN_DURATION_MS 12000
#define AUTO_CONNECT_WHEEL 1

// Direct connect to a known wheel MAC (skip scan). Set to 0 to use scan-only flow.
#define WHEEL_DIRECT_CONNECT 1
#define WHEEL_MAC_ADDRESS "88:25:83:f6:21:0f"
// Friendly name for UI / HUD (instead of the BLE advertised name). Empty = use BLE name.
#define WHEEL_DISPLAY_NAME "Apex"
#define WHEEL_DIRECT_SCAN_MS 10000
#define WHEEL_DIRECT_RETRY_MS 3000

// WiFi hotspot for web UI. NINA cannot run WiFi+BLE together unless firmware >= 3.0.1.
#define WIFI_AP_SSID "EUC-NANO"
#define WIFI_AP_PASSWORD "eucnano1"
#define WIFI_RETRY_MS 5000
