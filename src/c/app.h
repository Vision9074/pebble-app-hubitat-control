// Notifications the communication layer raises; main.c forwards each to
// whichever windows are on the stack.

#pragma once

// The device table gained or lost rows - menus must reload, not just redraw.
void app_devices_reloaded(void);

// Values changed in place; row counts are unchanged.
void app_states_updated(void);

// Connection state, mode or error text changed.
void app_status_changed(void);
