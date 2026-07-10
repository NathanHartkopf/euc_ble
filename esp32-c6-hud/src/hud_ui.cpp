#include "config.h"
#include "hud_ui.h"
#include "lv_font_speed_115.h"

#include <stdio.h>
#include <string.h>

namespace {

struct BarValueLabels {
  lv_obj_t *white = nullptr;
  lv_obj_t *black = nullptr;
};

lv_obj_t *status_label = nullptr;
lv_obj_t *speed_label = nullptr;
lv_obj_t *battery_track = nullptr;
lv_obj_t *battery_fill = nullptr;
BarValueLabels battery_labels;
lv_obj_t *temp_track = nullptr;
lv_obj_t *temp_fill = nullptr;
BarValueLabels temp_labels;
lv_obj_t *detail_label = nullptr;

constexpr int kSideBarW = 34;
constexpr int kSideBarH = 140;
constexpr int kSideColInset = 8;
constexpr int kSpeedOffsetX = 0;
constexpr int kBottomRowY = -8;
constexpr lv_coord_t kTwoDigitNudge = 12;

lv_coord_t speed_slot_w = 120;
lv_coord_t speed_digits_w = 100;

constexpr float kTempBarMaxF = 175.0f;
constexpr float kTempBarDefaultMinF = 50.0f;

constexpr float kKmhToMph = 0.621371f;
constexpr float kCtoFScale = 9.0f / 5.0f;
constexpr float kCtoFOffset = 32.0f;

float temp_bar_min_f = kTempBarDefaultMinF;
bool temp_scale_initialized = false;

float kmhToMph(float kmh) { return kmh * kKmhToMph; }

float celsiusToFahrenheit(float celsius) { return celsius * kCtoFScale + kCtoFOffset; }

void styleVerticalTrack(lv_obj_t *track) {
  lv_obj_set_style_bg_color(track, lv_color_hex(0x303030), 0);
  lv_obj_set_style_border_width(track, 0, 0);
  lv_obj_set_style_pad_all(track, 0, 0);
  lv_obj_set_style_radius(track, 4, 0);
  lv_obj_clear_flag(track, LV_OBJ_FLAG_SCROLLABLE);
}

void styleVerticalFill(lv_obj_t *fill, uint32_t color) {
  lv_obj_set_style_bg_color(fill, lv_color_hex(color), 0);
  lv_obj_set_style_border_width(fill, 0, 0);
  lv_obj_set_style_pad_all(fill, 0, 0);
  lv_obj_set_style_radius(fill, 4, 0);
  lv_obj_clear_flag(fill, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(fill, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
}

void styleBarValueLabel(lv_obj_t *label, lv_color_t color) {
  lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(label, color, 0);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_bg_opa(label, LV_OPA_TRANSP, 0);
  lv_obj_set_width(label, kSideBarW);
  lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
}

void createBarValueLabels(BarValueLabels &labels, lv_obj_t *track, lv_obj_t *fill) {
  labels.white = lv_label_create(track);
  styleBarValueLabel(labels.white, lv_color_hex(0xFFFFFF));
  lv_label_set_text(labels.white, "--");
  lv_obj_align(labels.white, LV_ALIGN_CENTER, 0, 0);

  labels.black = lv_label_create(fill);
  styleBarValueLabel(labels.black, lv_color_hex(0x000000));
  lv_label_set_text(labels.black, "--");
  lv_obj_align_to(labels.black, track, LV_ALIGN_CENTER, 0, 0);
}

void updateBarValueLabels(BarValueLabels &labels, lv_obj_t *track, lv_obj_t *fill,
                          const char *text) {
  if (!labels.white || !labels.black || !track || !fill) {
    return;
  }

  lv_label_set_text(labels.white, text);
  lv_label_set_text(labels.black, text);
  lv_obj_align(labels.white, LV_ALIGN_CENTER, 0, 0);
  lv_obj_align_to(labels.black, track, LV_ALIGN_CENTER, 0, 0);

  lv_obj_move_background(labels.white);
  lv_obj_move_foreground(fill);
  lv_obj_move_foreground(labels.black);
}

void setVerticalFill(lv_obj_t *track, lv_obj_t *fill, BarValueLabels *labels, int bar_h, float ratio,
                     uint32_t color) {
  if (!track || !fill) {
    return;
  }

  if (ratio < 0.0f) {
    ratio = 0.0f;
  }
  if (ratio > 1.0f) {
    ratio = 1.0f;
  }

  const lv_coord_t fill_h =
      static_cast<lv_coord_t>(ratio * static_cast<float>(bar_h) + 0.5f);
  lv_obj_set_height(fill, fill_h > 0 ? fill_h : 1);
  lv_obj_align(fill, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_bg_color(fill, lv_color_hex(color), 0);

  if (labels && labels->black) {
    lv_obj_align_to(labels->black, track, LV_ALIGN_CENTER, 0, 0);
    lv_obj_move_background(labels->white);
    lv_obj_move_foreground(fill);
    lv_obj_move_foreground(labels->black);
  }
}

uint32_t tempColor(float temp_c) {
  if (temp_c < 40.0f) {
    return 0x4CD964;
  }
  if (temp_c <= 60.0f) {
    return 0xFF9500;
  }
  return 0xFF3B30;
}

void resetTempScale() {
  temp_bar_min_f = kTempBarDefaultMinF;
  temp_scale_initialized = false;
}

void updateTempScale(float temp_f) {
  if (temp_scale_initialized) {
    return;
  }

  if (temp_f < kTempBarDefaultMinF) {
    temp_bar_min_f = temp_f;
  }
  temp_scale_initialized = true;
}

float tempFillRatio(float temp_f) {
  updateTempScale(temp_f);

  const float span = kTempBarMaxF - temp_bar_min_f;
  if (span <= 0.0f) {
    return 0.0f;
  }

  if (temp_f <= temp_bar_min_f) {
    return 0.0f;
  }
  if (temp_f >= kTempBarMaxF) {
    return 1.0f;
  }

  return (temp_f - temp_bar_min_f) / span;
}

void setBatteryFill(uint8_t pct, bool charging, const char *label_text) {
  const uint32_t color = charging ? 0x5AC8FA : 0x4CD964;
  setVerticalFill(battery_track, battery_fill, &battery_labels, kSideBarH,
                  static_cast<float>(pct) / 100.0f, color);
  updateBarValueLabels(battery_labels, battery_track, battery_fill, label_text);
}

void setTempFill(float temp_c, const char *label_text) {
  const float temp_f = celsiusToFahrenheit(temp_c);
  setVerticalFill(temp_track, temp_fill, &temp_labels, kSideBarH, tempFillRatio(temp_f),
                  tempColor(temp_c));
  updateBarValueLabels(temp_labels, temp_track, temp_fill, label_text);
}

void setStatusColor(bool connected) {
  if (!status_label) {
    return;
  }
  lv_obj_set_style_text_color(status_label, connected ? lv_color_hex(0x4CD964) : lv_color_hex(0xFF9500),
                              0);
}

constexpr uint32_t kBgNormal = 0x101010;
constexpr uint32_t kBgWarn = 0xFF9500;   // > 52 mph
constexpr uint32_t kBgDanger = 0xFF3B30; // > 55 mph

void setBackgroundForSpeed(float mph, bool have_speed) {
  uint32_t bg = kBgNormal;
  uint32_t speed_fg = 0xFFFFFF;
  if (have_speed) {
    if (mph > 55.0f) {
      bg = kBgDanger;
    } else if (mph > 52.0f) {
      bg = kBgWarn;
      speed_fg = 0x000000;
    }
  }
  lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(bg), 0);
  if (speed_label) {
    lv_obj_set_style_text_color(speed_label, lv_color_hex(speed_fg), 0);
  }
}

void setSpeedText(const char *text) {
  if (!speed_label || !text) {
    return;
  }

  lv_label_set_text(speed_label, text);

  lv_coord_t pad_left = 0;
  if (text[0] != '\0' && text[1] == '\0') {
    const lv_coord_t text_w =
        lv_txt_get_width(text, 1, &lv_font_speed_115, 0, LV_TEXT_FLAG_NONE);
    if (text_w < speed_slot_w) {
      pad_left = (speed_slot_w - text_w) / 2;
    }
  } else if (text[0] != '\0' && text[1] != '\0') {
    pad_left = kTwoDigitNudge;
  }

  lv_obj_set_style_pad_left(speed_label, pad_left, 0);
  lv_obj_align(speed_label, LV_ALIGN_CENTER, kSpeedOffsetX, -4);
}

}  // namespace

void hudUiInit(lv_disp_t *disp) {
  (void)disp;

  lv_obj_t *screen = lv_scr_act();
  lv_obj_set_style_bg_color(screen, lv_color_hex(0x101010), 0);

  status_label = lv_label_create(screen);
  lv_label_set_text(status_label, "Starting");
  lv_obj_set_style_text_font(status_label, &lv_font_montserrat_14, 0);
  lv_obj_align(status_label, LV_ALIGN_TOP_MID, 0, 4);

  battery_track = lv_obj_create(screen);
  lv_obj_set_size(battery_track, kSideBarW, kSideBarH);
  lv_obj_align(battery_track, LV_ALIGN_LEFT_MID, kSideColInset, 6);
  styleVerticalTrack(battery_track);

  battery_fill = lv_obj_create(battery_track);
  lv_obj_set_width(battery_fill, kSideBarW);
  lv_obj_set_height(battery_fill, 1);
  lv_obj_align(battery_fill, LV_ALIGN_BOTTOM_MID, 0, 0);
  styleVerticalFill(battery_fill, 0x4CD964);
  createBarValueLabels(battery_labels, battery_track, battery_fill);
  updateBarValueLabels(battery_labels, battery_track, battery_fill, "--");

  temp_track = lv_obj_create(screen);
  lv_obj_set_size(temp_track, kSideBarW, kSideBarH);
  lv_obj_align(temp_track, LV_ALIGN_RIGHT_MID, -kSideColInset, 6);
  styleVerticalTrack(temp_track);

  temp_fill = lv_obj_create(temp_track);
  lv_obj_set_width(temp_fill, kSideBarW);
  lv_obj_set_height(temp_fill, 1);
  lv_obj_align(temp_fill, LV_ALIGN_BOTTOM_MID, 0, 0);
  styleVerticalFill(temp_fill, 0x4CD964);
  createBarValueLabels(temp_labels, temp_track, temp_fill);
  updateBarValueLabels(temp_labels, temp_track, temp_fill, "--");

  // Fixed-width slot: widest 2-digit glyph pair + right-nudge room, then centered.
  speed_digits_w = 0;
  for (int tens = 0; tens <= 9; tens++) {
    for (int ones = 0; ones <= 9; ones++) {
      char pair[3] = {static_cast<char>('0' + tens), static_cast<char>('0' + ones), '\0'};
      const lv_coord_t w =
          lv_txt_get_width(pair, 2, &lv_font_speed_115, 0, LV_TEXT_FLAG_NONE);
      if (w > speed_digits_w) {
        speed_digits_w = w;
      }
    }
  }
  speed_digits_w += 4;
  if (speed_digits_w < 1) {
    speed_digits_w = 100;
  }
  speed_slot_w = speed_digits_w + kTwoDigitNudge;

  speed_label = lv_label_create(screen);
  lv_obj_set_width(speed_label, speed_slot_w);
  lv_label_set_long_mode(speed_label, LV_LABEL_LONG_CLIP);
  lv_obj_set_style_text_font(speed_label, &lv_font_speed_115, 0);
  lv_obj_set_style_text_color(speed_label, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_align(speed_label, LV_TEXT_ALIGN_LEFT, 0);
  lv_obj_set_style_bg_opa(speed_label, LV_OPA_TRANSP, 0);
  setSpeedText("--");

  // Keep side bars above the speed label if anything overlaps.
  lv_obj_move_foreground(battery_track);
  lv_obj_move_foreground(temp_track);

  detail_label = lv_label_create(screen);
  lv_label_set_text(detail_label, "Waiting for wheel");
  lv_obj_set_style_text_font(detail_label, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(detail_label, lv_color_hex(0xAAAAAA), 0);
  lv_obj_align(detail_label, LV_ALIGN_BOTTOM_MID, kSpeedOffsetX, kBottomRowY);
}

void hudUiUpdate(const HudView &view) {
  char line[80];

  snprintf(line, sizeof(line), "%s%s", view.status_text,
           view.proxy_client_connected ? " + DarknessBot" : "");
  lv_label_set_text(status_label, line);
  setStatusColor(view.connected);

  if (!view.connected || !view.telemetry.valid) {
    setBackgroundForSpeed(0.0f, false);
    setSpeedText("--");
    setBatteryFill(0, false, "--");
    resetTempScale();
    setVerticalFill(temp_track, temp_fill, &temp_labels, kSideBarH, 0.0f, 0x303030);
    updateBarValueLabels(temp_labels, temp_track, temp_fill, "--");
    lv_label_set_text(detail_label, "Waiting for wheel");
    return;
  }

  const veteran::Telemetry &t = view.telemetry;
  const float mph = kmhToMph(t.speed_kmh);
  const float temp_f = celsiusToFahrenheit(t.temp_c);

  setBackgroundForSpeed(mph, true);

  snprintf(line, sizeof(line), "%.0f", mph);
  setSpeedText(line);

  snprintf(line, sizeof(line), "%u%%", t.battery_pct);
  setBatteryFill(t.battery_pct, t.charging, line);

  snprintf(line, sizeof(line), "%.0f\u00B0", temp_f);
  setTempFill(t.temp_c, line);

  snprintf(line, sizeof(line), "%.1f V   %.1f A", t.voltage_v, t.current_a);
  lv_label_set_text(detail_label, line);
}
