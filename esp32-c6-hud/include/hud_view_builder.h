#pragma once

#include "euc_ble.h"
#include "hud_view.h"

inline HudView buildHudView(const EucBleClient &ble) {
  HudView view;
  view.connected = ble.isConnected();
  view.proxy_client_connected = ble.proxyClientConnected();
  view.status_text = ble.statusText();
  view.telemetry = ble.telemetry();
  return view;
}
