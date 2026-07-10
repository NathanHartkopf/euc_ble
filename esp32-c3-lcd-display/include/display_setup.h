#pragma once

#include <Arduino_GFX_Library.h>

#include "config.h"

// Landscape rotation (1): 320 x 172 visible area.
inline Arduino_DataBus *createDisplayBus() {
  return new Arduino_ESP32SPI(LCD_PIN_DC, LCD_PIN_CS, LCD_PIN_SCLK, LCD_PIN_MOSI);
}

inline Arduino_GFX *createDisplay(Arduino_DataBus *bus) {
  return new Arduino_ST7789(bus, LCD_PIN_RST, 1 /* landscape */, true /* IPS */, 172, 320,
                            LCD_COL_OFFSET_1, 0, LCD_COL_OFFSET_2, 0);
}
