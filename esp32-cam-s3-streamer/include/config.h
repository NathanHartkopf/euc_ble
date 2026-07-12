#pragma once

#include "board_config.h"
#include "esp_camera.h"

// QVGA is 320x240; receiver crops to 320x172 landscape.
#define CAMERA_FRAME_SIZE FRAMESIZE_QVGA
#define CAMERA_GRAB_MODE CAMERA_GRAB_LATEST
#define CAMERA_FB_LOCATION CAMERA_FB_IN_PSRAM

#if CAMERA_BOARD_XIAO_S3_SENSE
// OV2640 on XIAO Sense — Seeed examples use 10 MHz XCLK for reliable probe.
#define CAMERA_XCLK_HZ 10000000
#define CAMERA_JPEG_QUALITY 12
#define CAMERA_FB_COUNT 2
#define CAMERA_POWER_UP_MS 300

#elif CAMERA_BOARD_FREENOVE_S3
#define CAMERA_XCLK_HZ 10000000
#define CAMERA_JPEG_QUALITY 10
#define CAMERA_FB_COUNT 2
#define CAMERA_POWER_UP_MS 500

#endif
