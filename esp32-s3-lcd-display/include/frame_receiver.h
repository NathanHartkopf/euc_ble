#pragma once

#include <Arduino.h>

#include "config.h"
#include "video_protocol.h"

class FrameReceiver {
 public:
  FrameReceiver();
  ~FrameReceiver();

  FrameReceiver(const FrameReceiver &) = delete;
  FrameReceiver &operator=(const FrameReceiver &) = delete;

  bool begin(uint16_t port);
  bool poll();

  // Returns true when a complete frame is ready for display.
  bool acquireDisplayFrame(const uint8_t **data, size_t *size);
  void releaseDisplayFrame();

 private:
  static constexpr size_t kBufferSize = VIDEO_MAX_JPEG_SIZE;

  bool copyReadyFrame();
  void resetAssembly();

  uint8_t *assembly_buffer_ = nullptr;
  uint8_t *display_buffers_[DISPLAY_FRAME_SLOTS] = {};
  size_t display_sizes_[DISPLAY_FRAME_SLOTS] = {};

  uint32_t active_frame_id_ = 0;
  uint16_t expected_chunks_ = 0;
  uint16_t received_chunks_ = 0;
  size_t assembly_offset_ = 0;
  bool assembly_ready_ = false;

  int readable_slot_ = -1;
  int write_slot_ = 0;
  bool slot_busy_[DISPLAY_FRAME_SLOTS] = {};

  uint8_t chunk_received_[256] = {};
};
