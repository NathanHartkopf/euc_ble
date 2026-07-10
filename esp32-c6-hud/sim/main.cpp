#include <SDL.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <unistd.h>

#include "config.h"
#include "display_sdl.h"
#include "hud_ui.h"
#include "hud_view.h"
#include "nano_telemetry.h"
#include "serial_feed.h"
#include "veteran_protocol.h"

namespace {

uint32_t simMillis() { return static_cast<uint32_t>(SDL_GetTicks()); }

SerialFeed g_serial;
veteran::Telemetry g_telemetry;
bool g_serial_connected = false;
uint32_t g_last_telemetry_ms = 0;
char g_serial_status[48] = "No serial";

constexpr uint32_t kTelemetryStaleMs = 2000;

struct SimOptions {
  bool mock_mode = false;
  const char *port = nullptr;
  int baud = 115200;
};

void onSerialLine(const char *line, void *user_data) {
  (void)user_data;
  veteran::Telemetry parsed;
  if (!nanoTelemetryParseLine(line, parsed)) {
    return;
  }

  g_telemetry = parsed;
  g_serial_connected = true;
  g_last_telemetry_ms = simMillis();
}

bool pathExists(const char *path) {
  return access(path, R_OK | W_OK) == 0;
}

bool autoDetectNanoPort(char *out, size_t out_size) {
  const char *patterns[] = {"/dev/cu.usbmodem", "/dev/tty.usbmodem"};
  for (const char *prefix : patterns) {
    DIR *dir = opendir("/dev");
    if (!dir) {
      continue;
    }

    struct dirent *entry = nullptr;
    while ((entry = readdir(dir)) != nullptr) {
      if (strncmp(entry->d_name, prefix + 5, strlen(prefix + 5)) != 0) {
        continue;
      }

      snprintf(out, out_size, "/dev/%s", entry->d_name);
      closedir(dir);
      return true;
    }
    closedir(dir);
  }

  return false;
}

bool parseArgs(int argc, char **argv, SimOptions &opts) {
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--mock") == 0) {
      opts.mock_mode = true;
      continue;
    }

    if (strcmp(argv[i], "--port") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "--port requires a device path\n");
        return false;
      }
      opts.port = argv[++i];
      continue;
    }

    if (strcmp(argv[i], "--baud") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "--baud requires a number\n");
        return false;
      }
      opts.baud = atoi(argv[++i]);
      continue;
    }

    fprintf(stderr, "Unknown argument: %s\n", argv[i]);
  fprintf(stderr, "Usage: hud-sim [--mock] [--port /dev/cu.usbmodemXXXX] [--baud 115200]\n");
    return false;
  }

  return true;
}

bool beginSerialFeed(const SimOptions &opts) {
  char auto_port[64] = {};
  const char *port = opts.port;

  if (!port || port[0] == '\0') {
    if (!autoDetectNanoPort(auto_port, sizeof(auto_port))) {
      strncpy(g_serial_status, "No Nano port", sizeof(g_serial_status) - 1);
      fprintf(stderr, "No serial port found. Plug in the Nano or pass --port /dev/cu.usbmodemXXXX\n");
      return false;
    }
    port = auto_port;
    fprintf(stderr, "Auto-detected serial port: %s\n", port);
  }

  if (!pathExists(port)) {
    snprintf(g_serial_status, sizeof(g_serial_status), "Missing port");
    fprintf(stderr, "Serial port not found: %s\n", port);
    return false;
  }

  if (!serialFeedOpen(g_serial, port, opts.baud)) {
    strncpy(g_serial_status, "Serial error", sizeof(g_serial_status) - 1);
    return false;
  }

  strncpy(g_serial_status, "Nano serial", sizeof(g_serial_status) - 1);
  fprintf(stderr, "Live mode: reading HUD lines from Nano (use --mock for demo data)\n");
  fprintf(stderr, "Flash the Nano with SERIAL_HUD_STREAM enabled in config.h\n");
  return true;
}

HudView makeLiveView(uint32_t now_ms) {
  HudView view;

  if (!serialFeedIsOpen(g_serial)) {
    view.connected = false;
    view.status_text = g_serial_status;
    return view;
  }

  if (g_serial_connected && now_ms - g_last_telemetry_ms > kTelemetryStaleMs) {
    g_serial_connected = false;
    strncpy(g_serial_status, "Nano idle", sizeof(g_serial_status) - 1);
  }

  view.connected = g_serial_connected;
  if (view.connected && WHEEL_DISPLAY_NAME[0] != '\0') {
    view.status_text = WHEEL_DISPLAY_NAME;
  } else {
    view.status_text = g_serial_status;
  }
  view.proxy_client_connected = false;
  if (view.connected) {
    view.telemetry = g_telemetry;
  }
  return view;
}

HudView makeDemoView(uint32_t elapsed_ms) {
  HudView view;

  if (elapsed_ms < 2000) {
    view.connected = false;
    view.status_text = "Scanning";
    return view;
  }

  view.connected = true;
  view.status_text = (WHEEL_DISPLAY_NAME[0] != '\0') ? WHEEL_DISPLAY_NAME : REPEATER_DEVICE_NAME;
  view.proxy_client_connected = (elapsed_ms / 8000) % 2 == 1;

  constexpr float kMaxMph = 60.0f;
  constexpr float kMphToKmh = 1.0f / 0.621371f;
  constexpr uint32_t kHalfCycleMs = 10000;  // 10s up, 10s down
  constexpr uint32_t kCycleMs = kHalfCycleMs * 2;

  const uint32_t ride_ms = elapsed_ms - 2000;
  const uint32_t cycle_pos = ride_ms % kCycleMs;
  float mph = 0.0f;
  if (cycle_pos <= kHalfCycleMs) {
    mph = kMaxMph * (static_cast<float>(cycle_pos) / static_cast<float>(kHalfCycleMs));
  } else {
    mph = kMaxMph *
          (1.0f - static_cast<float>(cycle_pos - kHalfCycleMs) / static_cast<float>(kHalfCycleMs));
  }

  const float phase = elapsed_ms / 1000.0f;
  view.telemetry.valid = true;
  view.telemetry.speed_kmh = mph * kMphToKmh;
  view.telemetry.voltage_v = 148.5f + 1.2f * std::sin(phase * 0.2f);
  view.telemetry.current_a = 6.0f + 4.0f * std::sin(phase * 1.1f);
  view.telemetry.temp_c = 32.0f + 2.0f * std::sin(phase * 0.15f);
  view.telemetry.battery_pct = 72 + static_cast<uint8_t>(8 * std::sin(phase * 0.05f));
  view.telemetry.charging = (elapsed_ms / 12000) % 2 == 0;

  return view;
}

}  // namespace

int main(int argc, char **argv) {
  SimOptions opts;
  if (!parseArgs(argc, argv, opts)) {
    return 1;
  }

  if (!opts.mock_mode) {
    beginSerialFeed(opts);
  }

  if (!simDisplayBegin()) {
    fprintf(stderr, "Failed to init SDL display. Install SDL2: brew install sdl2\n");
    return 1;
  }

  hudUiInit(simDisplayGet());

  const uint32_t started = simMillis();
  uint32_t last_tick = started;
  uint32_t last_ui = started;

  while (true) {
    const uint32_t now = simMillis();

    if (!opts.mock_mode && serialFeedIsOpen(g_serial)) {
      serialFeedPoll(g_serial, now, onSerialLine, nullptr);
    }

    if (now - last_tick >= 5) {
      lv_tick_inc(now - last_tick);
      last_tick = now;
    }

    if (now - last_ui >= 100) {
      last_ui = now;
      if (opts.mock_mode) {
        hudUiUpdate(makeDemoView(now - started));
      } else {
        hudUiUpdate(makeLiveView(now));
      }
    }

    simDisplayLoop();
    SDL_Delay(5);
  }

  serialFeedClose(g_serial);
  simDisplayEnd();
  return 0;
}
