#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>

#include "config.h"
#include "video_protocol.h"
#include "esp_camera.h"
#include "camera_pins.h"

WiFiUDP udp;
uint32_t frame_id = 0;

bool initCamera() {
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
  config.frame_size = FRAMESIZE_QVGA;
  config.pixel_format = PIXFORMAT_JPEG;
  config.grab_mode = CAMERA_GRAB_LATEST;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.jpeg_quality = CAMERA_JPEG_QUALITY;
  config.fb_count = 2;

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed: 0x%x\n", err);
    return false;
  }

  sensor_t *sensor = esp_camera_sensor_get();
  if (sensor) {
    sensor->set_vflip(sensor, 1);
    sensor->set_hmirror(sensor, 0);
  }

  return true;
}

bool initWifiAp() {
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1),
                    IPAddress(255, 255, 255, 0));
  if (!WiFi.softAP(VIDEO_WIFI_SSID, VIDEO_WIFI_PASSWORD, 1, 0, 4)) {
    Serial.println("Failed to start WiFi AP");
    return false;
  }

  Serial.printf("AP ready: %s @ %s\n", VIDEO_WIFI_SSID, WiFi.softAPIP().toString().c_str());
  return true;
}

void sendFrame(const uint8_t *jpeg, size_t jpeg_len) {
  const size_t max_chunk = VIDEO_UDP_PAYLOAD_MAX - sizeof(VideoPacketHeader);
  const uint16_t chunk_count =
      static_cast<uint16_t>((jpeg_len + max_chunk - 1) / max_chunk);

  uint8_t packet[VIDEO_UDP_PAYLOAD_MAX];
  frame_id++;

  for (uint16_t chunk = 0; chunk < chunk_count; chunk++) {
    const size_t offset = static_cast<size_t>(chunk) * max_chunk;
    const size_t remaining = jpeg_len - offset;
    const uint16_t payload_size =
        static_cast<uint16_t>(remaining > max_chunk ? max_chunk : remaining);

    VideoPacketHeader *header = reinterpret_cast<VideoPacketHeader *>(packet);
    header->magic = VIDEO_MAGIC;
    header->frame_id = frame_id;
    header->chunk_index = chunk;
    header->chunk_count = chunk_count;
    header->payload_size = payload_size;
    memcpy(packet + sizeof(VideoPacketHeader), jpeg + offset, payload_size);

    const size_t packet_len = sizeof(VideoPacketHeader) + payload_size;
    udp.beginPacket(IPAddress(255, 255, 255, 255), VIDEO_UDP_PORT);
    udp.write(packet, packet_len);
    udp.endPacket();
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);

  if (!initCamera() || !initWifiAp()) {
    while (true) {
      delay(1000);
    }
  }

  udp.begin(VIDEO_UDP_PORT);
  Serial.println("Streaming JPEG frames over UDP");
}

void loop() {
  const uint32_t frame_interval_ms = 1000 / STREAM_TARGET_FPS;
  const uint32_t started = millis();

  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb || fb->format != PIXFORMAT_JPEG) {
    if (fb) {
      esp_camera_fb_return(fb);
    }
    delay(5);
    return;
  }

  if (fb->len > 0 && fb->len <= VIDEO_MAX_JPEG_SIZE) {
    sendFrame(fb->buf, fb->len);
  }

  esp_camera_fb_return(fb);

  const uint32_t elapsed = millis() - started;
  if (elapsed < frame_interval_ms) {
    delay(frame_interval_ms - elapsed);
  }
}
