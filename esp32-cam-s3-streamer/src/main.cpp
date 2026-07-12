#include <Arduino.h>
#include <ArduinoWebsockets.h>
#include <WiFi.h>
#include <esp_system.h>
#include <esp_wifi.h>

#include "board_config.h"
#include "camera_setup.h"
#include "config.h"
#include "status_led.h"
#include "video_protocol.h"
#include "esp_camera.h"

using namespace websockets;

WebsocketsServer ws_server;
WebsocketsClient ws_client;

bool ws_client_connected = false;
bool ap_ready = false;

uint32_t frame_id = 0;
uint8_t *send_buffer = nullptr;

constexpr uint8_t kApChannel = 1;
constexpr uint8_t kApMaxClients = 4;
constexpr uint32_t kApSettleMs = 400;
constexpr int kApRetryCount = 5;

bool initWifiAp();

bool restoreWifiAp() {
  ap_ready = initWifiAp();
  return ap_ready;
}

void listenStreamServer() { ws_server.listen(VIDEO_WS_PORT); }

bool initWifiAp() {
  WiFi.persistent(false);
  WiFi.mode(WIFI_OFF);
  delay(100);

  WiFi.mode(WIFI_AP);
  WiFi.setSleep(false);
  esp_wifi_set_ps(WIFI_PS_NONE);

  if (!WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1),
                         IPAddress(255, 255, 255, 0))) {
    Serial.println("softAPConfig failed");
  }

  for (int attempt = 1; attempt <= kApRetryCount; attempt++) {
    if (WiFi.softAP(VIDEO_WIFI_SSID, VIDEO_WIFI_PASSWORD, kApChannel, 0, kApMaxClients)) {
      delay(kApSettleMs);
      const IPAddress ip = WiFi.softAPIP();
      if (ip != IPAddress(0, 0, 0, 0)) {
        Serial.printf("AP ready: %s @ %s (attempt %d)\n", VIDEO_WIFI_SSID, ip.toString().c_str(),
                      attempt);
        return true;
      }
      Serial.printf("AP started but IP not assigned (attempt %d)\n", attempt);
    } else {
      Serial.printf("softAP failed (attempt %d)\n", attempt);
    }

    WiFi.softAPdisconnect(true);
    delay(300);
  }

  return false;
}

bool ensureCamera() {
  if (cameraEnsure(ap_ready, !ws_client_connected, restoreWifiAp, listenStreamServer)) {
    if (ws_client_connected) {
      statusLedSet(StatusColor::Green);
    }
    return true;
  }

  statusLedSet(StatusColor::Red);
  return false;
}

void pollWebSocketClient() {
  if (ws_server.poll()) {
    ws_client = ws_server.accept();
    ws_client.onEvent([](WebsocketsEvent event, String) {
      if (event == WebsocketsEvent::ConnectionClosed) {
        ws_client_connected = false;
        Serial.println("Display disconnected");
      }
    });
    ws_client_connected = true;
    Serial.println("Display connected via WebSocket");
    if (cameraIsReady()) {
      statusLedSet(StatusColor::Green);
    }
  }

  if (!ws_client_connected) {
    return;
  }

  ws_client.poll();
}

bool sendFrame(const uint8_t *jpeg, size_t jpeg_len) {
  if (!ws_client_connected || !send_buffer) {
    return false;
  }

  const size_t packet_len = VIDEO_FRAME_HEADER_SIZE + jpeg_len;
  if (packet_len > VIDEO_MAX_JPEG_SIZE + VIDEO_FRAME_HEADER_SIZE) {
    return false;
  }

  frame_id++;
  memcpy(send_buffer, &frame_id, VIDEO_FRAME_HEADER_SIZE);
  memcpy(send_buffer + VIDEO_FRAME_HEADER_SIZE, jpeg, jpeg_len);

  if (!ws_client.sendBinary(reinterpret_cast<const char *>(send_buffer), packet_len)) {
    Serial.println("WebSocket send dropped");
    return false;
  }

  statusLedPulseFrame();
  return true;
}

void bootLedPulse() {
  statusLedSet(StatusColor::Red);
  delay(200);
  statusLedSet(StatusColor::Off);
  delay(200);
}

void updateStatusLed() {
  if (cameraIsReady() && ws_client_connected) {
    statusLedSet(StatusColor::Green);
  } else if (cameraIsReady()) {
    statusLedSet(StatusColor::Blue);
  } else if (ap_ready) {
    static uint32_t last_blink_ms = 0;
    static bool blink_on = false;
    const uint32_t now = millis();
    if (now - last_blink_ms >= 500) {
      last_blink_ms = now;
      blink_on = !blink_on;
      statusLedSet(blink_on ? StatusColor::Red : StatusColor::Off);
    }
  } else {
    statusLedSet(StatusColor::Red);
  }
}

void setup() {
  statusLedBegin();
  bootLedPulse();
  bootLedPulse();
  bootLedPulse();

  Serial.begin(115200);
  delay(200);

#if CAMERA_BOARD_XIAO_S3_SENSE
  Serial.println("Board: Seeed XIAO ESP32-S3 Sense (OV2640)");
#else
  Serial.println("Board: Freenove ESP32-S3 WROOM CAM");
#endif
  Serial.printf("Boot: chip %s, PSRAM %u bytes, reset %d\n", ESP.getChipModel(),
                ESP.getFreePsram(), static_cast<int>(esp_reset_reason()));

  send_buffer = static_cast<uint8_t *>(ps_malloc(VIDEO_MAX_JPEG_SIZE + VIDEO_FRAME_HEADER_SIZE));
  if (!send_buffer) {
    Serial.println("Send buffer alloc failed");
    while (true) {
      bootLedPulse();
    }
  }

  ap_ready = initWifiAp();
  if (!ap_ready) {
    Serial.println("WiFi AP failed");
    while (true) {
      bootLedPulse();
    }
  }

  listenStreamServer();
  Serial.printf("WebSocket server listening on port %u\n", VIDEO_WS_PORT);
  Serial.println("AP up; starting camera in background");
}

void loop() {
  pollWebSocketClient();
  updateStatusLed();

  if (!cameraIsReady()) {
    static uint32_t last_camera_retry_ms = 0;
    static uint8_t failed_retries = 0;
    const uint32_t now = millis();
    const uint32_t retry_interval_ms = failed_retries > 10 ? 30000 : 5000;
    if (now - last_camera_retry_ms >= retry_interval_ms) {
      last_camera_retry_ms = now;
      if (!ensureCamera()) {
        failed_retries++;
      } else {
        failed_retries = 0;
        Serial.println("Camera ready");
      }
    }
    delay(10);
    return;
  }

  if (!ap_ready || WiFi.softAPIP() == IPAddress(0, 0, 0, 0)) {
    Serial.println("AP lost, restarting radio");
    ap_ready = initWifiAp();
    ws_client_connected = false;
    listenStreamServer();
    delay(500);
    return;
  }

  if (!ws_client_connected) {
    delay(10);
    return;
  }

  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb || fb->format != PIXFORMAT_JPEG) {
    if (fb) {
      esp_camera_fb_return(fb);
    }
    return;
  }

  if (fb->len > 0 && fb->len <= VIDEO_MAX_JPEG_SIZE) {
    sendFrame(fb->buf, fb->len);
  }

  esp_camera_fb_return(fb);
}
