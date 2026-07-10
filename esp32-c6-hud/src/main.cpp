#include <Arduino.h>

#include "config.h"
#include "display_lvgl.h"
#include "euc_ble.h"
#include "hud_ui.h"
#include "hud_view.h"
#include "hud_view_builder.h"
#include "io_expander.h"

namespace {

EucBleClient ble_client;
uint32_t last_hud_refresh_ms = 0;

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(200);

  Serial.printf("ESP32-C6 EUC HUD + BLE repeater — chip %s @ %u MHz\n", ESP.getChipModel(),
                getCpuFrequencyMhz());

  initBacklight();

  if (!displayLvglBegin()) {
    Serial.println("LVGL display init failed");
    while (true) {
      delay(1000);
    }
  }

  hudUiInit(displayLvglGet());
  hudUiUpdate(buildHudView(ble_client));

  if (!ble_client.begin()) {
    Serial.println("BLE init failed");
  }
}

void loop() {
  ble_client.loop();
  displayLvglLoop();

  const uint32_t now = millis();
  if (ble_client.consumeTelemetryUpdate() || now - last_hud_refresh_ms >= HUD_IDLE_REFRESH_MS) {
    last_hud_refresh_ms = now;
    hudUiUpdate(buildHudView(ble_client));
  }

  delay(5);
}
