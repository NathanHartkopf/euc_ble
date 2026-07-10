#include "nano_telemetry.h"

#include <stdio.h>
#include <string.h>

bool nanoTelemetryParseLine(const char *line, veteran::Telemetry &out) {
  if (!line) {
    return false;
  }

  if (strncmp(line, "HUD,", 4) == 0) {
    unsigned int batt = 0;
    unsigned int chg = 0;
    float spd = 0.0f;
    float volt = 0.0f;
    float amp = 0.0f;
    float tmp = 0.0f;

    const int matched = sscanf(line + 4, "%f,%f,%f,%u,%f,%u", &spd, &volt, &amp, &batt, &tmp, &chg);
    if (matched < 6) {
      return false;
    }

    out.speed_kmh = spd;
    out.voltage_v = volt;
    out.current_a = amp;
    out.battery_pct = static_cast<uint8_t>(batt > 100 ? 100 : batt);
    out.temp_c = tmp;
    out.charging = chg != 0;
    out.valid = true;
    return true;
  }

  float spd = 0.0f;
  float amp = 0.0f;
  float volt = 0.0f;
  unsigned int batt = 0;
  float tmp = 0.0f;
  char chg_text[8] = {};

  const int matched = sscanf(line, "spd=%f km/h  amp=%f A  volt=%f V  batt=%u%%  tmp=%f C", &spd,
                             &amp, &volt, &batt, &tmp);
  if (matched < 5) {
    return false;
  }

  const char *chg_pos = strstr(line, "chg=");
  if (chg_pos) {
    sscanf(chg_pos, "chg=%7s", chg_text);
  }

  out.speed_kmh = spd;
  out.current_a = amp;
  out.voltage_v = volt;
  out.battery_pct = static_cast<uint8_t>(batt > 100 ? 100 : batt);
  out.temp_c = tmp;
  out.charging = strcmp(chg_text, "yes") == 0;
  out.valid = true;
  return true;
}
