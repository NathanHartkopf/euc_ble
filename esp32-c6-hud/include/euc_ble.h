#pragma once

#include <Arduino.h>

#include "veteran_protocol.h"

enum class EucBleState : uint8_t {
  Idle,
  Scanning,
  Connecting,
  Connected,
};

class EucBleClient {
 public:
  bool begin();
  void loop();

  EucBleState state() const { return state_; }
  const char *statusText() const;
  bool isConnected() const { return state_ == EucBleState::Connected; }

  const veteran::Telemetry &telemetry() const { return telemetry_; }
  bool consumeTelemetryUpdate();

  const char *wheelName() const { return wheel_name_; }
  const char *wheelAddress() const { return wheel_address_; }
  const char *advertisedName() const { return advertised_name_; }
  bool proxyClientConnected() const { return proxy_client_count_ > 0; }

  // Used by NimBLE scan/notify callbacks.
  bool connectToDevice(const class NimBLEAdvertisedDevice *device);
  void handleDisconnect();
  void processTelemetryChunk(const uint8_t *data, size_t length);
  void forwardProxyWrite(const uint8_t *data, size_t length);
  void notifyProxyClientConnected();
  void notifyProxyClientDisconnected();

 private:
  enum class ScanMode : uint8_t {
    None,
    DirectMac,
  };

  EucBleState state_ = EucBleState::Idle;
  ScanMode scan_mode_ = ScanMode::None;
  veteran::FrameReassembler frame_parser_;
  veteran::Telemetry telemetry_;
  bool telemetry_updated_ = false;

  char wheel_name_[32] = {};
  char wheel_address_[18] = {};
  char advertised_name_[32] = "NF7266";

  uint16_t proxy_client_count_ = 0;
  uint32_t reconnect_at_ms_ = 0;
  uint32_t direct_scan_until_ms_ = 0;

  bool setupRepeater();
  void updateAdvertisedName(const char *name);
  void requestDirectConnect();
  void scheduleReconnect(uint32_t delay_ms);
  bool subscribeTelemetry();
};
