#pragma once

// Board profile is selected via build_flags in platformio.ini.

#if defined(BOARD_SEED_XIAO_ESP32S3_SENSE)
#define CAMERA_BOARD_XIAO_S3_SENSE 1

#elif defined(BOARD_FREENOVE_ESP32_S3_WROOM)
#define CAMERA_BOARD_FREENOVE_S3 1

#else
#error "Select a board profile in platformio.ini (BOARD_SEED_XIAO_ESP32S3_SENSE or BOARD_FREENOVE_ESP32_S3_WROOM)"
#endif
