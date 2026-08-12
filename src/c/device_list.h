// The scrolling list of devices. Up/down move the selection, select opens a
// device, and holding select toggles it without leaving the list.

#pragma once

#include <pebble.h>

void device_list_push(void);
void device_list_reload(void);
void device_list_deinit(void);
