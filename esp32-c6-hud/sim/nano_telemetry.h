#pragma once

#include "veteran_protocol.h"

bool nanoTelemetryParseLine(const char *line, veteran::Telemetry &out);
