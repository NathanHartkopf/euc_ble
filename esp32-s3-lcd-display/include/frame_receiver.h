#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "config.h"
#include "video_protocol.h"

class FrameReceiver {
 public:
  FrameReceiver();
  ~FrameReceiver();

  FrameReceiver(const FrameReceiver &) = delete;
  FrameReceiver &operator=(const FrameReceiver &) = delete;

  bool begin();
  bool poll();

  // Returns true when a complete frame is ready for display.
  bool acquireDisplayFrame(const uint8_t **data, size_t *size, uint32_t *frame_id);
  void releaseDisplayFrame(uint32_t frame_id);

  uint32_t lastDrawnFrameId() const { return last_drawn_frame_id_; }
  bool isConnected() const { return ws_connected_; }

 private:
  static constexpr size_t kBufferSize = VIDEO_MAX_JPEG_SIZE;

  void handleBinaryMessage(const uint8_t *data, size_t len);
  bool ensureConnected();

  SemaphoreHandle_t buffer_mutex_ = nullptr;
  uint8_t *frame_buffer_ = nullptr;
  uint8_t *decode_buffer_ = nullptr;
  size_t frame_size_ = 0;
  uint32_t pending_frame_id_ = 0;
  uint32_t last_drawn_frame_id_ = 0;
  bool frame_pending_ = false;
  bool ws_connected_ = false;

  void *ws_client_ = nullptr;
};
