#pragma once

#include "veteran_protocol.h"

struct HudView {
  bool connected = false;
  bool proxy_client_connected = false;
  const char *status_text = "Idle";
  veteran::Telemetry telemetry = {};
};
