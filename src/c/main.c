// Hubitat Control - a Pebble app for driving Hubitat devices through the
// hub's Maker API, over the local network or Hubitat's cloud relay.
//
// The watch holds no credentials and makes no HTTP calls; the phone-side
// JavaScript in src/pkjs owns the hub connection and hands the watch a device
// table over AppMessage. See README.md.

#include <pebble.h>

#include "app.h"
#include "comm.h"
#include "device_list.h"
#include "device_window.h"
#include "devices.h"
#include "main_menu.h"

void app_devices_reloaded(void) {
  device_list_reload();
  main_menu_reload();
}

void app_states_updated(void) {
  device_list_reload();
  device_window_states_updated();
}

void app_status_changed(void) {
  main_menu_reload();
  device_list_reload();
}

static void init(void) {
  comm_init();
  main_menu_push();
}

static void deinit(void) {
  comm_deinit();
  device_window_deinit();
  device_list_deinit();
  main_menu_deinit();
  devices_free();
}

int main(void) {
  init();
  app_event_loop();
  deinit();
  return 0;
}
