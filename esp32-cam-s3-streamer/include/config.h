#pragma once

// Freenove ESP32-S3 WROOM CAM (default). Change to AI_THINKER_ESP32S3_CAM if needed.
#define CAMERA_MODEL_FREENOVE_ESP32_S3_WROOM

// QVGA is 320x240; receiver crops to 320x172 landscape.
#define CAMERA_JPEG_QUALITY 14
#define CAMERA_XCLK_HZ 20000000

// Lower = faster stream, higher = better image.
#define STREAM_TARGET_FPS 15
