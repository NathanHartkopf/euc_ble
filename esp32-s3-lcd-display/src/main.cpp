#include <Arduino.h>
#include <JPEGDEC.h>

#include <WiFi.h>
#include <cstring>

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

void showSplash(const char *status) {
  gfx->fillScreen(gfx->color565(8, 18, 52));
  gfx->fillRect(0, 0, VIDEO_FRAME_WIDTH, 6, gfx->color565(0, 180, 220));
  gfx->fillRect(0, VIDEO_FRAME_HEIGHT - 6, VIDEO_FRAME_WIDTH, 6, gfx->color565(0, 180, 220));

  gfx->setTextColor(gfx->color565(255, 255, 255));
  gfx->setTextSize(2);
  gfx->setCursor((VIDEO_FRAME_WIDTH - 9 * 12) / 2, 42);
  gfx->println("EUC VIDEO");

  gfx->setTextSize(1);
  gfx->setTextColor(gfx->color565(140, 170, 210));
  gfx->setCursor((VIDEO_FRAME_WIDTH - 8 * 6) / 2, 72);
  gfx->println("Receiver");

  gfx->fillRect(40, 112, VIDEO_FRAME_WIDTH - 80, 14, gfx->color565(20, 36, 72));
  gfx->fillRect(40, 112, (VIDEO_FRAME_WIDTH - 80) / 3, 14, gfx->color565(0, 180, 220));

  if (status && status[0] != '\0') {
    const int16_t text_width = static_cast<int16_t>(strlen(status)) * 6;
    gfx->setTextColor(gfx->color565(255, 255, 255));
    gfx->setCursor((VIDEO_FRAME_WIDTH - text_width) / 2, 138);
    gfx->println(status);
  }
}

void showStatus(const char *message, uint16_t color) {
  Serial.printf("Status: %s\n", message);
  gfx->fillScreen(RGB565_BLACK);
  gfx->setTextColor(color, RGB565_BLACK);
  gfx->setTextSize(2);
  gfx->setCursor(8, (VIDEO_FRAME_HEIGHT - 16) / 2);
  gfx->println(message);
}

void drawFrame(const uint8_t *data, size_t size) {
  if (jpeg.openRAM(const_cast<uint8_t *>(data), size, jpegDrawCallback) != 1) {
    return;
  }

  crop_top = (jpeg.getHeight() > VIDEO_FRAME_HEIGHT) ? (jpeg.getHeight() - VIDEO_FRAME_HEIGHT) / 2 : 0;

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
    if (receiver.poll()) {
      xSemaphoreGive(frame_ready_sem);
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

    for (;;) {
      const uint8_t *data = nullptr;
      size_t size = 0;
      uint32_t frame_id = 0;
      if (!receiver.acquireDisplayFrame(&data, &size, &frame_id)) {
        break;
      }

      drawFrame(data, size);
      receiver.releaseDisplayFrame(frame_id);

      if (!receiver.poll()) {
        break;
      }
    }
  }
}
#endif

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(200);

  initBacklight();
  setBacklight(90);

  bus = createDisplayBus();
  gfx = createDisplay(bus);

  if (!beginDisplay(bus, gfx)) {
    Serial.println("Display init failed");
    while (true) {
      delay(1000);
    }
  }

  configureDisplayBus(bus);
  showSplash("Starting...");
  Serial.println("Display init ok");

  logPlatformInfo();
  showSplash("WiFi...");

  if (!connectWifi()) {
    showStatus("WiFi failed", RGB565_RED);
    while (true) {
      delay(1000);
    }
  }

  showSplash("WebSocket...");

  if (!receiver.begin()) {
    showStatus("Receiver failed", RGB565_RED);
    while (true) {
      delay(1000);
    }
  }

  showSplash("Waiting video");

#if USE_DUAL_CORE_PIPELINE
  frame_ready_sem = xSemaphoreCreateBinary();
  xTaskCreatePinnedToCore(networkTask, "video_net", 8192, nullptr, 2, &network_task_handle,
                          NETWORK_TASK_CORE);
  xTaskCreatePinnedToCore(displayTask, "video_lcd", 8192, nullptr, 1, nullptr, DISPLAY_TASK_CORE);
  Serial.println("Dual-core video pipeline active");
#endif
}

void loop() {
#if USE_DUAL_CORE_PIPELINE
  vTaskDelay(portMAX_DELAY);
#else
  if (receiver.poll()) {
    const uint8_t *data = nullptr;
    size_t size = 0;
    uint32_t frame_id = 0;
    if (receiver.acquireDisplayFrame(&data, &size, &frame_id)) {
      drawFrame(data, size);
      receiver.releaseDisplayFrame(frame_id);
    }
  }
#endif
}
