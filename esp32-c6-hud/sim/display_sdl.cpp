#include "display_sdl.h"

#include <SDL.h>

#include <cstdlib>
#include <cstring>

#include "config.h"

namespace {

constexpr int kScale = 3;

SDL_Window *window = nullptr;
SDL_Renderer *renderer = nullptr;
SDL_Texture *texture = nullptr;
uint16_t framebuffer[HUD_WIDTH * HUD_HEIGHT];

lv_disp_draw_buf_t draw_buf;
lv_disp_drv_t disp_drv;
lv_disp_t *disp = nullptr;
lv_color_t *buf1 = nullptr;

constexpr size_t kDrawBufLines = HUD_HEIGHT;
constexpr size_t kDrawBufPixels = HUD_WIDTH * kDrawBufLines;

void dispFlush(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p) {
  const int32_t w = area->x2 - area->x1 + 1;
  const int32_t h = area->y2 - area->y1 + 1;

  for (int32_t y = 0; y < h; y++) {
    uint16_t *dst = &framebuffer[(area->y1 + y) * HUD_WIDTH + area->x1];
    const uint16_t *src = reinterpret_cast<uint16_t *>(&color_p[y * w]);
    memcpy(dst, src, static_cast<size_t>(w) * sizeof(uint16_t));
  }

  lv_disp_flush_ready(disp_drv);
}

void presentFrame() {
  SDL_UpdateTexture(texture, nullptr, framebuffer, HUD_WIDTH * static_cast<int>(sizeof(uint16_t)));
  SDL_RenderClear(renderer);
  SDL_RenderCopy(renderer, texture, nullptr, nullptr);
  SDL_RenderPresent(renderer);
}

}  // namespace

bool simDisplayBegin() {
  if (SDL_Init(SDL_INIT_VIDEO) != 0) {
    return false;
  }

  window = SDL_CreateWindow("EUC HUD Simulator", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                            HUD_WIDTH * kScale, HUD_HEIGHT * kScale, SDL_WINDOW_SHOWN);
  if (!window) {
    return false;
  }

  renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  if (!renderer) {
    return false;
  }

  SDL_RenderSetLogicalSize(renderer, HUD_WIDTH, HUD_HEIGHT);
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");

  texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING, HUD_WIDTH,
                              HUD_HEIGHT);
  if (!texture) {
    return false;
  }

  memset(framebuffer, 0, sizeof(framebuffer));

  buf1 = static_cast<lv_color_t *>(malloc(kDrawBufPixels * sizeof(lv_color_t)));
  if (!buf1) {
    return false;
  }

  lv_init();
  lv_disp_draw_buf_init(&draw_buf, buf1, nullptr, kDrawBufPixels);
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = HUD_WIDTH;
  disp_drv.ver_res = HUD_HEIGHT;
  disp_drv.flush_cb = dispFlush;
  disp_drv.draw_buf = &draw_buf;
  disp = lv_disp_drv_register(&disp_drv);

  return disp != nullptr;
}

void simDisplayLoop() {
  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    if (event.type == SDL_QUIT) {
      exit(0);
    }
  }

  lv_timer_handler();
  presentFrame();
}

void simDisplayEnd() {
  free(buf1);
  buf1 = nullptr;

  if (texture) {
    SDL_DestroyTexture(texture);
    texture = nullptr;
  }
  if (renderer) {
    SDL_DestroyRenderer(renderer);
    renderer = nullptr;
  }
  if (window) {
    SDL_DestroyWindow(window);
    window = nullptr;
  }

  SDL_Quit();
}

lv_disp_t *simDisplayGet() { return disp; }
