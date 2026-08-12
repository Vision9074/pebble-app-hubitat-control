// The control screen for a single device: select toggles, up/down move a
// dimmer, volume or shade position.

#pragma once

#include <pebble.h>

void device_window_push(int32_t device_id);
void device_window_states_updated(void);
void device_window_deinit(void);
