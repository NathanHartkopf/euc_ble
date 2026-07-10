#pragma once

#include <Arduino.h>

#include "video_protocol.h"

class FrameReceiver {
 public:
  bool begin(uint16_t port);
  bool poll();
  bool hasCompleteFrame() const;
  const uint8_t *frameData() const;
  size_t frameSize() const;
  void releaseFrame();

 private:
  static constexpr size_t kBufferSize = VIDEO_MAX_JPEG_SIZE;

  uint32_t active_frame_id_ = 0;
  uint16_t expected_chunks_ = 0;
  uint16_t received_chunks_ = 0;
  size_t write_offset_ = 0;
  bool frame_ready_ = false;

  uint8_t chunk_received_[256] = {};
  uint8_t buffer_[kBufferSize] = {};
};
