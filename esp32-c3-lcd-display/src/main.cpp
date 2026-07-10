#include <Arduino.h>
#include <JPEGDEC.h>
#include <WiFi.h>

#include "config.h"
#include "display_setup.h"
#include "frame_receiver.h"
#include "io_expander.h"
#include "video_protocol.h"

namespace {

Arduino_DataBus *bus = nullptr;
Arduino_GFX *gfx = nullptr;
JPEGDEC jpeg;
FrameReceiver receiver;

int16_t crop_top = 0;

int jpegDrawCallback(JPEGDRAW *draw) {
  const int16_t y = draw->y - crop_top;
  if (y + draw->iHeight <= 0 || y >= VIDEO_FRAME_HEIGHT) {
    return 1;
  }

  gfx->draw16bitBeRGBBitmap(draw->x, y, draw->pPixels, draw->iWidth, draw->iHeight);
  return 1;
}

bool connectWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(VIDEO_WIFI_SSID, VIDEO_WIFI_PASSWORD);

  const uint32_t timeout_ms = 30000;
  const uint32_t started = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - started > timeout_ms) {
      return false;
    }
    delay(250);
  }

  Serial.printf("Connected to %s, IP %s\n", VIDEO_WIFI_SSID, WiFi.localIP().toString().c_str());
  return true;
}

void showStatus(const char *message, uint16_t color) {
  gfx->fillScreen(RGB565_BLACK);
  gfx->setCursor(8, 8);
  gfx->setTextColor(color, RGB565_BLACK);
  gfx->setTextSize(2);
  gfx->println(message);
}

void drawFrame(const uint8_t *data, size_t size) {
  if (jpeg.openRAM(const_cast<uint8_t *>(data), size, jpegDrawCallback) != 1) {
    return;
  }

  crop_top = (jpeg.getHeight() > VIDEO_FRAME_HEIGHT) ? (jpeg.getHeight() - VIDEO_FRAME_HEIGHT) / 2 : 0;

  gfx->fillScreen(RGB565_BLACK);
  jpeg.setPixelType(RGB565_BIG_ENDIAN);
  jpeg.decode(0, 0, 0);
  jpeg.close();
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(200);

  initIoExpanderBacklight();

  bus = createDisplayBus();
  gfx = createDisplay(bus);

  if (!gfx->begin()) {
    Serial.println("Display init failed");
    while (true) {
      delay(1000);
    }
  }

  gfx->fillScreen(RGB565_BLACK);
  showStatus("WiFi...", RGB565_YELLOW);

  if (!connectWifi()) {
    showStatus("WiFi failed", RGB565_RED);
    while (true) {
      delay(1000);
    }
  }

  if (!receiver.begin(VIDEO_UDP_PORT)) {
    showStatus("UDP failed", RGB565_RED);
    while (true) {
      delay(1000);
    }
  }

  showStatus("Waiting video", RGB565_GREEN);
}

void loop() {
  while (receiver.poll()) {
  }

  if (receiver.hasCompleteFrame()) {
    drawFrame(receiver.frameData(), receiver.frameSize());
    receiver.releaseFrame();
  }
}
