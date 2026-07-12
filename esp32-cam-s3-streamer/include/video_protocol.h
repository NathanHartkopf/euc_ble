#pragma once

#include <stdint.h>

// Shared between camera streamer and LCD receiver.
#define VIDEO_WIFI_SSID "EUC-VIDEO"
#define VIDEO_WIFI_PASSWORD "eucvideo1"

// Camera runs the AP; display connects as STA then opens a WebSocket client.
#define VIDEO_CAM_HOST "192.168.4.1"
#define VIDEO_WS_PORT 80

// Landscape target for the 1.47" panel (172x320 portrait -> 320x172 landscape).
#define VIDEO_FRAME_WIDTH 320
#define VIDEO_FRAME_HEIGHT 172

#define VIDEO_MAX_JPEG_SIZE (32 * 1024)

// Binary WebSocket payload: [frame_id:4][jpeg bytes...]
#define VIDEO_FRAME_HEADER_SIZE 4
