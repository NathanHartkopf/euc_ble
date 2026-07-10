#pragma once

#include <Arduino.h>

inline void initBacklight() {
  pinMode(LCD_PIN_BL, OUTPUT);
  analogWrite(LCD_PIN_BL, LCD_BACKLIGHT_PWM);
}
