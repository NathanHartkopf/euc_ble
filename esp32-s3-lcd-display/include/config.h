#pragma once

// Target: Waveshare ESP32-S3-LCD-1.47 (16 MB flash, 8 MB OPI PSRAM, dual-core @ 240 MHz).
// Set the active profile in platformio.ini build_flags.

#if defined(BOARD_WAVESHARE_ESP32_S3_LCD_147)
#define IO_EXPANDER_I2C_SDA -1
#define IO_EXPANDER_I2C_SCL -1
#define IO_EXPANDER_I2C_ADDR 0
#define USE_IO_EXPANDER_BACKLIGHT 0
#elif defined(BOARD_WAVESHARE_ESP32_C3_LCD_147)
#define IO_EXPANDER_I2C_SDA 3
#define IO_EXPANDER_I2C_SCL 4
#define IO_EXPANDER_I2C_ADDR 0x24
#define USE_IO_EXPANDER_BACKLIGHT 1
#elif defined(BOARD_WAVESHARE_ESP32_C6_LCD_147)
#define USE_IO_EXPANDER_BACKLIGHT 0
#else
#error "Select a board profile in platformio.ini build_flags"
#endif

// ST7789 SPI — Waveshare ESP32-S3-LCD-1.47 pinout.
#if defined(BOARD_WAVESHARE_ESP32_S3_LCD_147)
#define LCD_PIN_MOSI 45
#define LCD_PIN_SCLK 40
#define LCD_PIN_CS 42
#define LCD_PIN_DC 41
#define LCD_PIN_RST 39
#define LCD_PIN_BL 48
#elif defined(BOARD_WAVESHARE_ESP32_C3_LCD_147) || defined(BOARD_WAVESHARE_ESP32_C6_LCD_147)
#define LCD_PIN_MOSI 6
#define LCD_PIN_SCLK 7
#define LCD_PIN_CS 14
#define LCD_PIN_DC 15
#define LCD_PIN_RST 21
#define LCD_PIN_BL 22
#endif

// ST7789 172x320 offsets for the 1.47" panel.
#define LCD_COL_OFFSET_1 34
#define LCD_COL_OFFSET_2 34

#define LCD_BACKLIGHT_PWM 128
#define LCD_SPI_FREQUENCY_HZ 40000000

// S3 has headroom for PSRAM frame buffers and a future LVGL overlay layer.
#define USE_PSRAM_BUFFERS 1
#define USE_DUAL_CORE_PIPELINE 1
#define NETWORK_TASK_CORE 0
#define DISPLAY_TASK_CORE 1
#define DISPLAY_FRAME_SLOTS 2
