#include "frame_receiver.h"

#include <ArduinoWebsockets.h>
#include <cstring>

#include "psram_alloc.h"

namespace {

using namespace websockets;

}  // namespace

FrameReceiver::FrameReceiver() {
  frame_buffer_ = static_cast<uint8_t *>(psramAlloc(kBufferSize));
  decode_buffer_ = static_cast<uint8_t *>(psramAlloc(kBufferSize));
  buffer_mutex_ = xSemaphoreCreateMutex();
  ws_client_ = new WebsocketsClient();
}

FrameReceiver::~FrameReceiver() {
  if (ws_client_) {
    auto *client = static_cast<WebsocketsClient *>(ws_client_);
    client->close();
    delete client;
    ws_client_ = nullptr;
  }

  if (buffer_mutex_) {
    vSemaphoreDelete(buffer_mutex_);
    buffer_mutex_ = nullptr;
  }

  psramFree(frame_buffer_);
  psramFree(decode_buffer_);
}

bool FrameReceiver::ensureConnected() {
  auto *client = static_cast<WebsocketsClient *>(ws_client_);
  if (!client) {
    return false;
  }

  if (ws_connected_) {
    return true;
  }

  const String url =
      String("ws://") + VIDEO_CAM_HOST + ":" + String(VIDEO_WS_PORT) + "/";
  if (!client->connect(url.c_str())) {
    return false;
  }

  ws_connected_ = true;
  Serial.println("WebSocket connected");
  return true;
}

bool FrameReceiver::begin() {
  if (!frame_buffer_ || !decode_buffer_ || !buffer_mutex_ || !ws_client_) {
    return false;
  }

  auto *client = static_cast<WebsocketsClient *>(ws_client_);
  client->onMessage([this](WebsocketsMessage message) {
    if (!message.isBinary()) {
      return;
    }
    handleBinaryMessage(reinterpret_cast<const uint8_t *>(message.c_str()), message.length());
  });

  client->onEvent([this](WebsocketsEvent event, String) {
    if (event == WebsocketsEvent::ConnectionClosed) {
      ws_connected_ = false;
      Serial.println("WebSocket disconnected");
    }
  });

  return true;
}

void FrameReceiver::handleBinaryMessage(const uint8_t *data, size_t len) {
  if (len <= VIDEO_FRAME_HEADER_SIZE) {
    return;
  }

  uint32_t frame_id = 0;
  memcpy(&frame_id, data, VIDEO_FRAME_HEADER_SIZE);

  const uint8_t *jpeg = data + VIDEO_FRAME_HEADER_SIZE;
  const size_t jpeg_len = len - VIDEO_FRAME_HEADER_SIZE;
  if (jpeg_len == 0 || jpeg_len > kBufferSize) {
    return;
  }

  if (xSemaphoreTake(buffer_mutex_, pdMS_TO_TICKS(5)) != pdTRUE) {
    return;
  }

  if (frame_id <= last_drawn_frame_id_) {
    xSemaphoreGive(buffer_mutex_);
    return;
  }

  if (!frame_pending_ || frame_id > pending_frame_id_) {
    memcpy(frame_buffer_, jpeg, jpeg_len);
    frame_size_ = jpeg_len;
    pending_frame_id_ = frame_id;
    frame_pending_ = true;
  }

  xSemaphoreGive(buffer_mutex_);
}

bool FrameReceiver::poll() {
  auto *client = static_cast<WebsocketsClient *>(ws_client_);
  if (!client) {
    return false;
  }

  if (!ws_connected_) {
    static uint32_t last_connect_attempt_ms = 0;
    const uint32_t now = millis();
    if (now - last_connect_attempt_ms >= 1000) {
      last_connect_attempt_ms = now;
      ensureConnected();
    }
    return false;
  }

  client->poll();

  if (xSemaphoreTake(buffer_mutex_, 0) != pdTRUE) {
    return false;
  }

  const bool ready = frame_pending_ && pending_frame_id_ > last_drawn_frame_id_;
  xSemaphoreGive(buffer_mutex_);
  return ready;
}

bool FrameReceiver::acquireDisplayFrame(const uint8_t **data, size_t *size, uint32_t *frame_id) {
  if (xSemaphoreTake(buffer_mutex_, portMAX_DELAY) != pdTRUE) {
    return false;
  }

  if (!frame_pending_ || pending_frame_id_ <= last_drawn_frame_id_) {
    xSemaphoreGive(buffer_mutex_);
    return false;
  }

  memcpy(decode_buffer_, frame_buffer_, frame_size_);
  *data = decode_buffer_;
  *size = frame_size_;
  *frame_id = pending_frame_id_;
  xSemaphoreGive(buffer_mutex_);
  return true;
}

void FrameReceiver::releaseDisplayFrame(uint32_t frame_id) {
  if (xSemaphoreTake(buffer_mutex_, portMAX_DELAY) != pdTRUE) {
    return;
  }

  if (frame_id > last_drawn_frame_id_) {
    last_drawn_frame_id_ = frame_id;
  }

  if (pending_frame_id_ <= last_drawn_frame_id_) {
    frame_pending_ = false;
  }

  xSemaphoreGive(buffer_mutex_);
}
