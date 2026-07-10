#pragma once

#include <Arduino.h>
#include <Wire.h>

#include "config.h"

namespace {

constexpr uint8_t IO_EXTENSION_PWM_ADDR = 0x05;

}  // namespace

inline void initIoExpanderBacklight() {
#if USE_IO_EXPANDER_BACKLIGHT
  Wire.begin(IO_EXPANDER_I2C_SDA, IO_EXPANDER_I2C_SCL);
  Wire.beginTransmission(IO_EXPANDER_I2C_ADDR);
  Wire.write(IO_EXTENSION_PWM_ADDR);
  Wire.write(LCD_BACKLIGHT_PWM);
  Wire.endTransmission();
#else
  pinMode(LCD_PIN_BL, OUTPUT);
  digitalWrite(LCD_PIN_BL, HIGH);
#endif
}
