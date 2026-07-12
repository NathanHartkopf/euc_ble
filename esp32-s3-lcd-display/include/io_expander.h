#pragma once

#include <Arduino.h>
#include <Wire.h>

#include "config.h"

namespace {

constexpr uint8_t IO_EXTENSION_PWM_ADDR = 0x05;

}  // namespace

inline void initBacklight() {
#if USE_IO_EXPANDER_BACKLIGHT
  Wire.begin(IO_EXPANDER_I2C_SDA, IO_EXPANDER_I2C_SCL);
  Wire.beginTransmission(IO_EXPANDER_I2C_ADDR);
  Wire.write(IO_EXTENSION_PWM_ADDR);
  Wire.write(LCD_BACKLIGHT_PWM);
  Wire.endTransmission();
#else
  pinMode(LCD_PIN_BL, OUTPUT);
  digitalWrite(LCD_PIN_BL, HIGH);
  analogWrite(LCD_PIN_BL, LCD_BACKLIGHT_PWM);
#endif
}

inline void setBacklight(uint8_t percent) {
#if USE_IO_EXPANDER_BACKLIGHT
  const uint8_t duty = static_cast<uint8_t>((static_cast<uint16_t>(percent) * 255) / 100);
  Wire.beginTransmission(IO_EXPANDER_I2C_ADDR);
  Wire.write(IO_EXTENSION_PWM_ADDR);
  Wire.write(duty);
  Wire.endTransmission();
#else
  if (percent == 0) {
    digitalWrite(LCD_PIN_BL, LOW);
    return;
  }
  digitalWrite(LCD_PIN_BL, HIGH);
  analogWrite(LCD_PIN_BL, static_cast<uint8_t>((static_cast<uint16_t>(percent) * 255) / 100));
#endif
}
