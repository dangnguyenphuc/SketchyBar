#pragma once
#include "event.h"

void mouse_begin(void);

// Observes mouse-down events everywhere on screen (listen-only, no permission
// needed for mouse events) and posts them as MOUSE_DOWN_GLOBAL. Mouse events
// otherwise only reach us for our own windows.
void mouse_watch_outside_clicks(bool enabled);
