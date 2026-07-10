#include "display_lvgl.h"

#include <Arduino_GFX_Library.h>

#include "config.h"

namespace {

Arduino_DataBus *bus = nullptr;
Arduino_GFX *gfx = nullptr;
lv_disp_draw_buf_t draw_buf;
lv_disp_drv_t disp_drv;
lv_color_t *buf1 = nullptr;
lv_disp_t *disp = nullptr;

constexpr size_t kDrawBufLines = HUD_HEIGHT;
constexpr size_t kDrawBufPixels = HUD_WIDTH * kDrawBufLines;

void lvglFlush(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p) {
  const uint32_t width = area->x2 - area->x1 + 1;
  const uint32_t height = area->y2 - area->y1 + 1;

  gfx->draw16bitRGBBitmap(area->x1, area->y1, reinterpret_cast<uint16_t *>(color_p), width, height);
  lv_disp_flush_ready(disp_drv);
}

}  // namespace

bool displayLvglBegin() {
  bus = new Arduino_ESP32SPI(LCD_PIN_DC, LCD_PIN_CS, LCD_PIN_SCLK, LCD_PIN_MOSI);
  gfx = new Arduino_ST7789(bus, LCD_PIN_RST, 1 /* landscape */, true /* IPS */, HUD_HEIGHT, HUD_WIDTH,
                            LCD_COL_OFFSET_1, 0, LCD_COL_OFFSET_2, 0);

  if (!gfx->begin()) {
    return false;
  }

  gfx->fillScreen(RGB565_BLACK);

  buf1 = static_cast<lv_color_t *>(malloc(kDrawBufPixels * sizeof(lv_color_t)));
  if (!buf1) {
    return false;
  }

  lv_init();

  lv_disp_draw_buf_init(&draw_buf, buf1, nullptr, kDrawBufPixels);
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = HUD_WIDTH;
  disp_drv.ver_res = HUD_HEIGHT;
  disp_drv.flush_cb = lvglFlush;
  disp_drv.draw_buf = &draw_buf;
  disp = lv_disp_drv_register(&disp_drv);

  return disp != nullptr;
}

void displayLvglLoop() { lv_timer_handler(); }

lv_disp_t *displayLvglGet() { return disp; }
