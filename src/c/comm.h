// AppMessage transport. The watch never speaks HTTP: it asks the phone for a
// list, for states, or to run a command, and the phone talks to the Maker API.

#pragma once

#include <pebble.h>

typedef enum {
  LinkWaiting = 0,  // phone side has not checked in yet
  LinkLoading,
  LinkReady,
  LinkError
} LinkState;

#define MODE_LOCAL 0
#define MODE_CLOUD 1

void comm_init(void);
void comm_deinit(void);

// force_refresh discards the phone's cached list and re-reads the hub.
void comm_request_list(bool force_refresh);
// Re-reads every device's current state without disturbing the list.
void comm_request_states(void);
// Re-reads one device, used when its control screen opens.
void comm_request_device(int32_t device_id);

// cmd is one of "on", "off", "level", "push"; the phone maps it onto the
// Maker API command this device actually accepts.
void comm_send_command(int32_t device_id, const char *cmd, int32_t arg);

void comm_set_mode(int mode);
int comm_mode(void);

// False when the phone reports that cloud access is turned off or unconfigured,
// so the Connection row can decline to offer it.
bool comm_cloud_available(void);

// True only if the user has deliberately opted in to unlocking doors from the
// list's hold-to-toggle shortcut. Off unless the phone says otherwise.
bool comm_quick_lock_allowed(void);

LinkState comm_link_state(void);
const char *comm_status_text(void);
// Percentage points a single up/down press moves a dimmer or volume.
int comm_step(void);
