#pragma once

#include <lvgl.h>

bool simDisplayBegin();
void simDisplayLoop();
void simDisplayEnd();
lv_disp_t *simDisplayGet();
