#pragma once

#include <lvgl.h>

#include "hud_view.h"

void hudUiInit(lv_disp_t *disp);
void hudUiUpdate(const HudView &view);
