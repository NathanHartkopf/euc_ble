#pragma once

#include <Arduino.h>

// Restores WiFi AP and WebSocket listener after a camera-init pause.
using CameraWifiRestoreFn = bool (*)();
using CameraStreamListenFn = void (*)();

bool cameraIsReady();
bool cameraInit(bool pause_wifi, CameraWifiRestoreFn restore_wifi, CameraStreamListenFn listen_stream);
bool cameraEnsure(bool ap_ready, bool pause_wifi_ok, CameraWifiRestoreFn restore_wifi,
                  CameraStreamListenFn listen_stream);
void cameraScanBus();
