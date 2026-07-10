#pragma once

// Waveshare ESP32-C6-LCD-1.47 (4 MB flash, 512 KB SRAM, BLE 5).
// ST7789 172x320 panel, landscape HUD area 320x172.

#define LCD_PIN_MOSI 6
#define LCD_PIN_SCLK 7
#define LCD_PIN_CS 14
#define LCD_PIN_DC 15
#define LCD_PIN_RST 21
#define LCD_PIN_BL 22

#define LCD_COL_OFFSET_1 34
#define LCD_COL_OFFSET_2 34
#define LCD_BACKLIGHT_PWM 128

#define HUD_WIDTH 320
#define HUD_HEIGHT 172

// Nosfet Apex — Veteran/LeaperKim protocol on HM-10 style GATT (from Nano project).
#define EUC_SERVICE_UUID "0000ffe0-0000-1000-8000-00805f9b34fb"
#define EUC_TELEMETRY_UUID "0000ffe1-0000-1000-8000-00805f9b34fb"
#define EUC_SERVICE_UUID_SHORT "ffe0"

#define EUC_NAME_HINT_COUNT 4
static const char *const EUC_NAME_HINTS[EUC_NAME_HINT_COUNT] = {
    "NF7266",
    "NOSFET",
    "APEX",
    "EUC",
};

// Advertised peripheral name for DarknessBot / other apps. Updated from the wheel when known.
#define REPEATER_DEVICE_NAME "NF7266"

#define BLE_DEBUG_SCAN 0
#define BLE_DEBUG_NOTIFY 0
#define BLE_DEBUG_PROXY 0
#define EUC_RECONNECT_DELAY_MS 1500

#define WHEEL_DIRECT_CONNECT 1
#define WHEEL_MAC_ADDRESS "88:25:83:f6:21:0f"
// Shown on the HUD when connected (instead of the BLE advertised name). Empty = use BLE name.
#define WHEEL_DISPLAY_NAME "Apex"
#define WHEEL_DIRECT_SCAN_MS 10000
#define WHEEL_DIRECT_RETRY_MS 3000

// HUD refresh while telemetry is idle.
#define HUD_IDLE_REFRESH_MS 500
