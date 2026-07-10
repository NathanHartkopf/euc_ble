#include "frame_receiver.h"

#include <WiFiUdp.h>

namespace {

WiFiUDP g_udp;
constexpr size_t kMaxChunkPayload = VIDEO_UDP_PAYLOAD_MAX - sizeof(VideoPacketHeader);

}  // namespace

bool FrameReceiver::begin(uint16_t port) {
  return g_udp.begin(port);
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
    expected_chunks_ = header.chunk_count;
    received_chunks_ = 0;
    write_offset_ = 0;
    frame_ready_ = false;
    memset(chunk_received_, 0, sizeof(chunk_received_));
  }

  if (chunk_received_[header.chunk_index]) {
    return false;
  }

  const size_t offset = static_cast<size_t>(header.chunk_index) * kMaxChunkPayload;
  if (offset + header.payload_size > kBufferSize) {
    return false;
  }

  memcpy(buffer_ + offset, packet + sizeof(VideoPacketHeader), header.payload_size);
  chunk_received_[header.chunk_index] = 1;
  received_chunks_++;

  if (header.chunk_index == header.chunk_count - 1) {
    write_offset_ = offset + header.payload_size;
  }

  if (received_chunks_ == expected_chunks_) {
    frame_ready_ = true;
  }

  return frame_ready_;
}

bool FrameReceiver::hasCompleteFrame() const { return frame_ready_; }

const uint8_t *FrameReceiver::frameData() const { return buffer_; }

size_t FrameReceiver::frameSize() const { return write_offset_; }

void FrameReceiver::releaseFrame() {
  frame_ready_ = false;
  received_chunks_ = 0;
  write_offset_ = 0;
  memset(chunk_received_, 0, sizeof(chunk_received_));
}
