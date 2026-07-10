#pragma once

#include <Arduino.h>

namespace veteran {

constexpr uint8_t kMagic0 = 0xDC;
constexpr uint8_t kMagic1 = 0x5A;
constexpr uint8_t kMagic2 = 0x5C;
constexpr uint8_t kLongFrameThreshold = 38;
constexpr size_t kMaxFrameBytes = 128;

struct Telemetry {
  float voltage_v = 0;
  float speed_kmh = 0;
  float current_a = 0;
  float temp_c = 0;
  float trip_m = 0;
  float total_m = 0;
  uint8_t battery_pct = 0;
  bool charging = false;
  uint16_t firmware_ver = 0;
  bool valid = false;
};

inline uint16_t readBe16(const uint8_t *data, size_t offset) {
  return (static_cast<uint16_t>(data[offset]) << 8) | data[offset + 1];
}

inline int16_t readBeI16(const uint8_t *data, size_t offset) {
  return static_cast<int16_t>(readBe16(data, offset));
}

inline uint32_t readWordSwapped32(const uint8_t *data, size_t offset) {
  return (static_cast<uint32_t>(data[offset + 2]) << 24) |
         (static_cast<uint32_t>(data[offset + 3]) << 16) |
         (static_cast<uint32_t>(data[offset]) << 8) |
         data[offset + 1];
}

inline uint32_t crc32(const uint8_t *data, size_t length) {
  uint32_t crc = 0xFFFFFFFF;

  for (size_t i = 0; i < length; i++) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; bit++) {
      const uint32_t mask = -(crc & 1u);
      crc = (crc >> 1) ^ (0xEDB88320u & mask);
    }
  }

  return ~crc;
}

inline uint8_t batteryPercentLynxFamily(uint16_t voltageCentivolts) {
  if (voltageCentivolts <= 11902) {
    return 0;
  }
  if (voltageCentivolts >= 14805) {
    return 100;
  }

  return static_cast<uint8_t>((voltageCentivolts - 11902) / 29.03f + 0.5f);
}

inline bool parseRealtimeFrame(const uint8_t *frame, size_t length, Telemetry &out) {
  if (length < 20 || frame[0] != kMagic0 || frame[1] != kMagic1 || frame[2] != kMagic2) {
    return false;
  }

  const uint8_t declaredLen = frame[3];
  if (declaredLen < 16) {
    return false;
  }

  const uint16_t voltageRaw = readBe16(frame, 4);
  out.voltage_v = voltageRaw / 100.0f;
  out.speed_kmh = readBeI16(frame, 6) / 10.0f;
  out.trip_m = readWordSwapped32(frame, 8);
  out.total_m = readWordSwapped32(frame, 12);
  out.current_a = readBeI16(frame, 16) / 10.0f;
  out.temp_c = readBeI16(frame, 18) / 100.0f;
  out.charging = (length > 23) ? (readBe16(frame, 22) > 0) : false;
  out.firmware_ver = (length > 29) ? readBe16(frame, 28) : 0;
  out.battery_pct = batteryPercentLynxFamily(voltageRaw);
  out.valid = true;
  return true;
}

class FrameReassembler {
 public:
  void reset() {
    length_ = 0;
  }

  void feed(const uint8_t *data, size_t dataLen) {
    for (size_t i = 0; i < dataLen; i++) {
      pushByte(data[i]);
      tryExtract();
    }
  }

  bool hasTelemetry() const {
    return pending_.valid;
  }

  Telemetry consumeTelemetry() {
    Telemetry result = pending_;
    pending_.valid = false;
    return result;
  }

 private:
  uint8_t buffer_[kMaxFrameBytes] = {};
  size_t length_ = 0;
  Telemetry pending_;

  void pushByte(uint8_t byte) {
    if (length_ >= kMaxFrameBytes) {
      shiftOne();
    }
    buffer_[length_++] = byte;
  }

  void shiftOne() {
    if (length_ <= 1) {
      length_ = 0;
      return;
    }

    memmove(buffer_, buffer_ + 1, length_ - 1);
    length_--;
  }

  int findMagic() const {
    for (size_t i = 0; i + 2 < length_; i++) {
      if (buffer_[i] == kMagic0 && buffer_[i + 1] == kMagic1 && buffer_[i + 2] == kMagic2) {
        return static_cast<int>(i);
      }
    }
    return -1;
  }

  void trimToMagic() {
    const int magicAt = findMagic();
    if (magicAt < 0) {
      if (length_ > 2) {
        length_ = 2;
      }
      return;
    }

    if (magicAt > 0) {
      memmove(buffer_, buffer_ + magicAt, length_ - magicAt);
      length_ -= magicAt;
    }
  }

  bool frameCrcValid(const uint8_t *frame, uint8_t declaredLen) const {
    if (declaredLen <= kLongFrameThreshold) {
      return true;
    }

    const size_t total = static_cast<size_t>(declaredLen) + 4;
    if (total < 8) {
      return false;
    }

    const uint32_t expected = (static_cast<uint32_t>(frame[declaredLen]) << 24) |
                              (static_cast<uint32_t>(frame[declaredLen + 1]) << 16) |
                              (static_cast<uint32_t>(frame[declaredLen + 2]) << 8) |
                              frame[declaredLen + 3];
    const uint32_t actual = crc32(frame, declaredLen);
    return actual == expected;
  }

  void dropFrame(size_t totalBytes) {
    if (totalBytes >= length_) {
      length_ = 0;
      return;
    }

    memmove(buffer_, buffer_ + totalBytes, length_ - totalBytes);
    length_ -= totalBytes;
  }

  void tryExtract() {
    while (length_ >= 4) {
      trimToMagic();
      if (length_ < 4) {
        return;
      }

      const uint8_t declaredLen = buffer_[3];
      const size_t total = static_cast<size_t>(declaredLen) + 4;
      if (total > kMaxFrameBytes || declaredLen < 16) {
        shiftOne();
        continue;
      }
      if (length_ < total) {
        return;
      }

      if (!frameCrcValid(buffer_, declaredLen)) {
        shiftOne();
        continue;
      }

      Telemetry parsed;
      if (parseRealtimeFrame(buffer_, total, parsed)) {
        pending_ = parsed;
      }

      dropFrame(total);
    }
  }
};

inline void printTelemetry(const Telemetry &t) {
  Serial.print(F("spd="));
  Serial.print(t.speed_kmh, 1);
  Serial.print(F(" km/h  amp="));
  Serial.print(t.current_a, 1);
  Serial.print(F(" A  volt="));
  Serial.print(t.voltage_v, 1);
  Serial.print(F(" V  batt="));
  Serial.print(t.battery_pct);
  Serial.print(F("%  tmp="));
  Serial.print(t.temp_c, 1);
  Serial.print(F(" C  trip="));
  Serial.print(t.trip_m / 1000.0f, 2);
  Serial.print(F(" km  odo="));
  Serial.print(t.total_m / 1000.0f, 1);
  Serial.print(F(" km  fw="));
  Serial.print(t.firmware_ver);
  Serial.print(F("  chg="));
  Serial.println(t.charging ? F("yes") : F("no"));
}

}  // namespace veteran
