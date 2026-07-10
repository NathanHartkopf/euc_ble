#pragma once

// Waveshare 1.47" ST7789 boards (172x320 portrait panel).
// Set the active profile in platformio.ini build_flags.

#if defined(BOARD_WAVESHARE_ESP32_C3_LCD_147)
#define IO_EXPANDER_I2C_SDA 3
#define IO_EXPANDER_I2C_SCL 4
#define IO_EXPANDER_I2C_ADDR 0x24
#define USE_IO_EXPANDER_BACKLIGHT 1
#elif defined(BOARD_WAVESHARE_ESP32_C6_LCD_147)
#define USE_IO_EXPANDER_BACKLIGHT 0
#else
#error "Select a board profile in config.h"
#endif

// ST7789 SPI (same on C3/C6 Waveshare 1.47" boards).
#define LCD_PIN_MOSI 6
#define LCD_PIN_SCLK 7
#define LCD_PIN_CS 14
#define LCD_PIN_DC 15
#define LCD_PIN_RST 21
#define LCD_PIN_BL 22

// ST7789 172x320 offsets for this panel.
#define LCD_COL_OFFSET_1 34
#define LCD_COL_OFFSET_2 34

// Keep backlight moderate (Waveshare recommends <= 50%).
#define LCD_BACKLIGHT_PWM 128
