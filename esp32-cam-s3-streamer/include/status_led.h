#pragma once

#include <Arduino.h>

#include "board_config.h"

enum class StatusColor : uint8_t { Off, Red, Green, Blue };

#if CAMERA_BOARD_XIAO_S3_SENSE

// XIAO user LED on GPIO 21 — active LOW (inverted).
constexpr uint8_t STATUS_LED_PIN = 21;
constexpr bool STATUS_LED_INVERTED = true;

inline void statusLedWrite(bool on) {
  pinMode(STATUS_LED_PIN, OUTPUT);
  digitalWrite(STATUS_LED_PIN, STATUS_LED_INVERTED ? !on : on);
}

inline void statusLedBegin() {
  statusLedWrite(false);
}

inline void statusLedSet(StatusColor color) {
  switch (color) {
    case StatusColor::Red:
    case StatusColor::Green:
    case StatusColor::Blue:
      statusLedWrite(true);
      break;
    case StatusColor::Off:
    default:
      statusLedWrite(false);
      break;
  }
}

inline void statusLedPulseFrame() {
  statusLedWrite(false);
  delayMicroseconds(400);
  statusLedWrite(true);
}

#else  // Freenove — WS2812 on GPIO 48 + blue LED on GPIO 2.

#include <Adafruit_NeoPixel.h>

constexpr uint8_t STATUS_LED_PIN = 48;
constexpr uint8_t STATUS_LED_BLUE_PIN = 2;

inline Adafruit_NeoPixel &statusPixel() {
  static Adafruit_NeoPixel pixel(1, STATUS_LED_PIN, NEO_GRB + NEO_KHZ800);
  return pixel;
}

inline void statusLedBegin() {
  pinMode(STATUS_LED_BLUE_PIN, OUTPUT);
  digitalWrite(STATUS_LED_BLUE_PIN, LOW);
  statusPixel().begin();
  statusPixel().setBrightness(32);
  statusPixel().show();
}

inline void statusLedSet(StatusColor color) {
  uint32_t rgb = 0;
  switch (color) {
    case StatusColor::Red:
      rgb = statusPixel().Color(255, 0, 0);
      break;
    case StatusColor::Green:
      rgb = statusPixel().Color(0, 255, 0);
      break;
    case StatusColor::Blue:
      rgb = statusPixel().Color(0, 0, 255);
      break;
    case StatusColor::Off:
    default:
      break;
  }
  statusPixel().setPixelColor(0, rgb);
  statusPixel().show();
}

inline void statusLedPulseFrame() {
  digitalWrite(STATUS_LED_BLUE_PIN, HIGH);
  statusLedSet(StatusColor::Blue);
  delay(1);
  digitalWrite(STATUS_LED_BLUE_PIN, LOW);
  statusLedSet(StatusColor::Green);
}

#endif
