#pragma once

#include <stdint.h>

// Shared between camera streamer and LCD receiver.
#define VIDEO_WIFI_SSID "EUC-VIDEO"
#define VIDEO_WIFI_PASSWORD "eucvideo1"

#define VIDEO_UDP_PORT 5555
#define VIDEO_MAGIC 0x45564356u  // 'EVCV'

// Landscape target for the 1.47" panel (172x320 portrait -> 320x172 landscape).
#define VIDEO_FRAME_WIDTH 320
#define VIDEO_FRAME_HEIGHT 172

#define VIDEO_MAX_JPEG_SIZE (32 * 1024)
#define VIDEO_UDP_PAYLOAD_MAX 1400

#pragma pack(push, 1)
typedef struct {
  uint32_t magic;
  uint32_t frame_id;
  uint16_t chunk_index;
  uint16_t chunk_count;
  uint16_t payload_size;
} VideoPacketHeader;
#pragma pack(pop)

static_assert(sizeof(VideoPacketHeader) == 14, "VideoPacketHeader must be 14 bytes");
