#pragma once

#include <Arduino.h>

namespace ninebot {

struct Telemetry {
  float speed_kmh = 0;
  float amp = 0;
  float volt = 0;
  float trip_km = 0;
  float trip_total_km = 0;
  float trip_remaining_km = 0;
  float temp_c = 0;
  float avg_speed_kmh = 0;
  uint16_t runtime_min = 0;
  uint8_t riding_mode = 0;
  uint8_t lock = 0;
  bool valid = false;
};

inline uint16_t readLe16(const uint8_t *data, size_t offset) {
  return static_cast<uint16_t>(data[offset]) | (static_cast<uint16_t>(data[offset + 1]) << 8);
}

inline uint32_t readLe32(const uint8_t *data, size_t offset) {
  return static_cast<uint32_t>(data[offset]) | (static_cast<uint32_t>(data[offset + 1]) << 8) |
         (static_cast<uint32_t>(data[offset + 2]) << 16) |
         (static_cast<uint32_t>(data[offset + 3]) << 24);
}

// Read commands from eucWatch eucNinebot.js (Ninebot C/E/P, service FFE0 / char FFE1).
inline const uint8_t *commandBytes(uint8_t slot, size_t *length) {
  static const uint8_t kAmp[] = {0x55, 0xAA, 0x03, 0x09, 0x01, 0x50, 0x02, 0xA0, 0xFF};
  static const uint8_t kSpeed[] = {0x55, 0xAA, 0x03, 0x09, 0x01, 0x26, 0x02, 0xCA, 0xFF};
  static const uint8_t kTemp[] = {0x55, 0xAA, 0x03, 0x09, 0x01, 0x3E, 0x02, 0xB2, 0xFF};
  static const uint8_t kVolt[] = {0x55, 0xAA, 0x03, 0x09, 0x01, 0x47, 0x02, 0xA9, 0xFF};
  static const uint8_t kTrip[] = {0x55, 0xAA, 0x03, 0x09, 0x01, 0xB9, 0x02, 0x37, 0xFF};
  static const uint8_t kRuntime[] = {0x55, 0xAA, 0x03, 0x09, 0x01, 0x3A, 0x02, 0xB6, 0xFF};
  static const uint8_t kRemaining[] = {0x55, 0xAA, 0x03, 0x09, 0x01, 0x25, 0x02, 0xCB, 0xFF};
  static const uint8_t kTotal[] = {0x55, 0xAA, 0x03, 0x09, 0x01, 0x29, 0x04, 0xC5, 0xFF};
  static const uint8_t kAvgSpeed[] = {0x55, 0xAA, 0x03, 0x09, 0x01, 0xB6, 0x02, 0x3A, 0xFF};
  static const uint8_t kLock[] = {0x55, 0xAA, 0x03, 0x09, 0x01, 0x70, 0x02, 0x80, 0xFF};

  switch (slot % 9) {
    case 0:
      *length = sizeof(kAmp);
      return kAmp;
    case 1:
      *length = sizeof(kSpeed);
      return kSpeed;
    case 2:
      *length = sizeof(kTemp);
      return kTemp;
    case 3:
      *length = sizeof(kVolt);
      return kVolt;
    case 4:
      *length = sizeof(kTrip);
      return kTrip;
    case 5:
      *length = sizeof(kRuntime);
      return kRuntime;
    case 6:
      *length = sizeof(kRemaining);
      return kRemaining;
    case 7:
      *length = sizeof(kTotal);
      return kTotal;
    default:
      *length = sizeof(kAvgSpeed);
      return kAvgSpeed;
  }
}

inline const uint8_t *ringOffCommand(size_t *length) {
  static const uint8_t kRingOff[] = {0x55, 0xAA, 0x04, 0x09, 0x03, 0xC6, 0x01, 0x00, 0x28, 0xFF};
  *length = sizeof(kRingOff);
  return kRingOff;
}

inline bool parseNotification(const uint8_t *data, size_t length, Telemetry &out) {
  if (length < 8) {
    return false;
  }

  const uint8_t variable = data[5];
  const uint16_t value16 = readLe16(data, 6);
  out.valid = true;

  switch (variable) {
    case 0x26:
      out.speed_kmh = value16 / 1000.0f;
      break;
    case 0x50:
      out.amp = (value16 > 32768) ? (value16 - 65536) / 100.0f : value16 / 100.0f;
      break;
    case 0x47:
      out.volt = value16 / 100.0f;
      break;
    case 0xB9:
      out.trip_km = value16 / 100.0f;
      break;
    case 0x29:
      out.trip_total_km = readLe32(data, 6) / 1000.0f;
      break;
    case 0x25:
      out.trip_remaining_km = value16 / 100.0f;
      break;
    case 0x3E:
      out.temp_c = value16 / 10.0f;
      break;
    case 0xB6:
      out.avg_speed_kmh = value16 / 1000.0f;
      break;
    case 0x3A:
      out.runtime_min = static_cast<uint16_t>(value16 / 60);
      break;
    case 0xD2:
      out.riding_mode = static_cast<uint8_t>(value16);
      break;
    case 0x70:
      out.lock = static_cast<uint8_t>(value16);
      break;
    default:
      return false;
  }

  return true;
}

inline void printTelemetry(const Telemetry &t) {
  Serial.print(F("spd="));
  Serial.print(t.speed_kmh, 2);
  Serial.print(F(" km/h  amp="));
  Serial.print(t.amp, 2);
  Serial.print(F(" A  volt="));
  Serial.print(t.volt, 2);
  Serial.print(F(" V  tmp="));
  Serial.print(t.temp_c, 1);
  Serial.print(F(" C  trip="));
  Serial.print(t.trip_km, 2);
  Serial.print(F(" km  rem="));
  Serial.print(t.trip_remaining_km, 2);
  Serial.print(F(" km  tot="));
  Serial.print(t.trip_total_km, 2);
  Serial.print(F(" km  avg="));
  Serial.print(t.avg_speed_kmh, 2);
  Serial.print(F(" km/h  run="));
  Serial.print(t.runtime_min);
  Serial.print(F(" min  mode="));
  Serial.print(t.riding_mode);
  Serial.print(F("  lock="));
  Serial.println(t.lock);
}

}  // namespace ninebot
