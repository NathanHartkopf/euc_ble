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

#if USE_DUAL_CORE_PIPELINE
SemaphoreHandle_t frame_ready_sem = nullptr;
TaskHandle_t network_task_handle = nullptr;
#endif

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

void logPlatformInfo() {
#if defined(BOARD_HAS_PSRAM) && USE_PSRAM_BUFFERS
  Serial.printf("PSRAM: %u bytes free\n", ESP.getFreePsram());
#endif
  Serial.printf("Chip: %s @ %u MHz, cores: %d\n", ESP.getChipModel(), getCpuFrequencyMhz(),
                ESP.getChipCores());
}

#if USE_DUAL_CORE_PIPELINE
void networkTask(void *param) {
  (void)param;
  for (;;) {
    while (receiver.poll()) {
      xSemaphoreGive(frame_ready_sem);
      ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    }
    vTaskDelay(1);
  }
}

void displayTask(void *param) {
  (void)param;
  for (;;) {
    if (xSemaphoreTake(frame_ready_sem, portMAX_DELAY) != pdTRUE) {
      continue;
    }

    const uint8_t *data = nullptr;
    size_t size = 0;
    if (receiver.acquireDisplayFrame(&data, &size)) {
      drawFrame(data, size);
      receiver.releaseDisplayFrame();
    }

    xTaskNotifyGive(network_task_handle);
  }
}
#endif

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(200);
  logPlatformInfo();

  initIoExpanderBacklight();

  bus = createDisplayBus();
  gfx = createDisplay(bus);

  if (!gfx->begin()) {
    Serial.println("Display init failed");
    while (true) {
      delay(1000);
    }
  }

  configureDisplayBus(bus);
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

#if USE_DUAL_CORE_PIPELINE
  frame_ready_sem = xSemaphoreCreateBinary();
  xTaskCreatePinnedToCore(networkTask, "video_net", 6144, nullptr, 2, &network_task_handle,
                          NETWORK_TASK_CORE);
  xTaskCreatePinnedToCore(displayTask, "video_lcd", 8192, nullptr, 1, nullptr, DISPLAY_TASK_CORE);
  Serial.println("Dual-core video pipeline active");
#endif
}

void loop() {
#if USE_DUAL_CORE_PIPELINE
  vTaskDelay(portMAX_DELAY);
#else
  while (receiver.poll()) {
  }

  const uint8_t *data = nullptr;
  size_t size = 0;
  if (receiver.acquireDisplayFrame(&data, &size)) {
    drawFrame(data, size);
    receiver.releaseDisplayFrame();
  }
#endif
}
