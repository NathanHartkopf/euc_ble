#include "camera_setup.h"

#include <WiFi.h>
#include <Wire.h>

#include "camera_pins.h"
#include "config.h"
#include "esp_camera.h"

namespace {

bool s_camera_ready = false;
bool s_driver_active = false;

void configureSensor() {
  sensor_t *sensor = esp_camera_sensor_get();
  if (!sensor) {
    return;
  }

  const uint16_t pid = sensor->id.PID;
  Serial.printf("Camera sensor PID 0x%04X\n", pid);

  if (pid == OV2640_PID) {
    sensor->set_hmirror(sensor, 0);
    sensor->set_vflip(sensor, 1);
    sensor->set_brightness(sensor, 0);
    sensor->set_saturation(sensor, 0);
    sensor->set_contrast(sensor, 1);
    sensor->set_quality(sensor, CAMERA_JPEG_QUALITY);
  } else if (pid == OV3660_PID) {
    sensor->set_hmirror(sensor, 1);
    sensor->set_vflip(sensor, 0);
    sensor->set_brightness(sensor, 1);
    sensor->set_saturation(sensor, 0);
    sensor->set_ae_level(sensor, -3);
  } else {
    sensor->set_hmirror(sensor, 1);
    sensor->set_vflip(sensor, 0);
  }
}

camera_config_t buildConfig() {
  camera_config_t config = {};
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = CAMERA_XCLK_HZ;
  config.frame_size = CAMERA_FRAME_SIZE;
  config.pixel_format = PIXFORMAT_JPEG;
  config.grab_mode = CAMERA_GRAB_MODE;
  config.fb_location = CAMERA_FB_LOCATION;
  config.jpeg_quality = CAMERA_JPEG_QUALITY;
  config.fb_count = CAMERA_FB_COUNT;
  return config;
}

}  // namespace

bool cameraIsReady() { return s_camera_ready; }

void cameraScanBus() {
  Wire.begin(SIOD_GPIO_NUM, SIOC_GPIO_NUM);
  Wire.setClock(100000);
  Serial.print("SCCB scan: ");
  bool found = false;
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.printf("0x%02X ", addr);
      found = true;
    }
  }
  if (!found) {
    Serial.print("no devices (expect 0x30 for OV2640)");
  }
  Serial.println();
}

bool cameraInit(bool pause_wifi, CameraWifiRestoreFn restore_wifi, CameraStreamListenFn listen_stream) {
  if (s_driver_active) {
    esp_camera_deinit();
    s_driver_active = false;
    delay(100);
  }

  const bool restore_ap = pause_wifi;
  if (restore_ap) {
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    delay(200);
  }

  delay(CAMERA_POWER_UP_MS);

  camera_config_t config = buildConfig();
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed: 0x%x\n", err);
    cameraScanBus();
    if (restore_ap && restore_wifi) {
      restore_wifi();
      if (listen_stream) {
        listen_stream();
      }
    }
    return false;
  }

  s_driver_active = true;
  configureSensor();

  if (restore_ap && restore_wifi) {
    restore_wifi();
    if (listen_stream) {
      listen_stream();
    }
  }

  Serial.println("Camera init ok");
  return true;
}

bool cameraEnsure(bool ap_ready, bool pause_wifi_ok, CameraWifiRestoreFn restore_wifi,
                  CameraStreamListenFn listen_stream) {
  if (s_camera_ready) {
    return true;
  }

  const bool pause_wifi = ap_ready && pause_wifi_ok;
  if (cameraInit(pause_wifi, restore_wifi, listen_stream)) {
    s_camera_ready = true;
    return true;
  }

  return false;
}
