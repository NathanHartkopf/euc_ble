#include "frame_receiver.h"

#include <WiFiUdp.h>

#include "config.h"
#include "psram_alloc.h"

namespace {

WiFiUDP g_udp;
constexpr size_t kMaxChunkPayload = VIDEO_UDP_PAYLOAD_MAX - sizeof(VideoPacketHeader);

}  // namespace

FrameReceiver::FrameReceiver() {
#if USE_PSRAM_BUFFERS
  assembly_buffer_ = static_cast<uint8_t *>(psramAlloc(kBufferSize));
  for (int i = 0; i < DISPLAY_FRAME_SLOTS; i++) {
    display_buffers_[i] = static_cast<uint8_t *>(psramAlloc(kBufferSize));
  }
#else
  assembly_buffer_ = new uint8_t[kBufferSize];
  display_buffers_[0] = assembly_buffer_;
#endif
}

FrameReceiver::~FrameReceiver() {
#if USE_PSRAM_BUFFERS
  psramFree(assembly_buffer_);
  for (int i = 0; i < DISPLAY_FRAME_SLOTS; i++) {
    psramFree(display_buffers_[i]);
  }
#else
  delete[] assembly_buffer_;
#endif
}

bool FrameReceiver::begin(uint16_t port) {
#if USE_PSRAM_BUFFERS
  if (!assembly_buffer_) {
    return false;
  }
  for (int i = 0; i < DISPLAY_FRAME_SLOTS; i++) {
    if (!display_buffers_[i]) {
      return false;
    }
  }
#else
  if (!assembly_buffer_) {
    return false;
  }
#endif
  return g_udp.begin(port);
}

void FrameReceiver::resetAssembly() {
  expected_chunks_ = 0;
  received_chunks_ = 0;
  assembly_offset_ = 0;
  assembly_ready_ = false;
  memset(chunk_received_, 0, sizeof(chunk_received_));
}

bool FrameReceiver::copyReadyFrame() {
#if !USE_PSRAM_BUFFERS
  readable_slot_ = 0;
  display_sizes_[0] = assembly_offset_;
  return true;
#else
  if (slot_busy_[write_slot_]) {
  drop_frame:
    resetAssembly();
    return false;
  }

  memcpy(display_buffers_[write_slot_], assembly_buffer_, assembly_offset_);
  display_sizes_[write_slot_] = assembly_offset_;
  readable_slot_ = write_slot_;
  slot_busy_[write_slot_] = true;
  write_slot_ = (write_slot_ + 1) % DISPLAY_FRAME_SLOTS;
  resetAssembly();
  return true;
#endif
}

bool FrameReceiver::poll() {
  int packet_size = g_udp.parsePacket();
  if (packet_size <= 0) {
    return false;
  }

  uint8_t packet[VIDEO_UDP_PAYLOAD_MAX];
  const int read_len = g_udp.read(packet, sizeof(packet));
  if (read_len < static_cast<int>(sizeof(VideoPacketHeader))) {
    return false;
  }

  VideoPacketHeader header;
  memcpy(&header, packet, sizeof(header));

  if (header.magic != VIDEO_MAGIC || header.payload_size == 0) {
    return false;
  }

  const int expected_len = static_cast<int>(sizeof(VideoPacketHeader) + header.payload_size);
  if (read_len < expected_len || header.chunk_count == 0 || header.chunk_count > 256) {
    return false;
  }

  if (header.chunk_index >= header.chunk_count) {
    return false;
  }

  if (header.frame_id != active_frame_id_) {
    active_frame_id_ = header.frame_id;
    resetAssembly();
    expected_chunks_ = header.chunk_count;
  }

  if (chunk_received_[header.chunk_index]) {
    return false;
  }

  const size_t offset = static_cast<size_t>(header.chunk_index) * kMaxChunkPayload;
  if (offset + header.payload_size > kBufferSize) {
    return false;
  }

  memcpy(assembly_buffer_ + offset, packet + sizeof(VideoPacketHeader), header.payload_size);
  chunk_received_[header.chunk_index] = 1;
  received_chunks_++;

  if (header.chunk_index == header.chunk_count - 1) {
    assembly_offset_ = offset + header.payload_size;
  }

  if (received_chunks_ == expected_chunks_) {
    assembly_ready_ = true;
    return copyReadyFrame();
  }

  return false;
}

bool FrameReceiver::acquireDisplayFrame(const uint8_t **data, size_t *size) {
  if (readable_slot_ < 0) {
    return false;
  }

  *data = display_buffers_[readable_slot_];
  *size = display_sizes_[readable_slot_];
  return true;
}

void FrameReceiver::releaseDisplayFrame() {
  if (readable_slot_ < 0) {
    return;
  }

#if USE_PSRAM_BUFFERS
  slot_busy_[readable_slot_] = false;
#endif
  readable_slot_ = -1;
}
