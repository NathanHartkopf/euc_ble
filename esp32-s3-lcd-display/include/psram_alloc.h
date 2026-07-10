#pragma once

#include <Arduino.h>
#include <esp_heap_caps.h>

inline void *psramAlloc(size_t size) {
#if defined(BOARD_HAS_PSRAM)
  void *ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (ptr) {
    return ptr;
  }
#endif
  return malloc(size);
}

inline void psramFree(void *ptr) {
  if (ptr) {
    heap_caps_free(ptr);
  }
}
