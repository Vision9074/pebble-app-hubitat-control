// Closes the app after a spell with no input, so it does not sit open on the
// wrist keeping the screen out of its watchface.

#pragma once

#include <pebble.h>

void idle_init(void);
void idle_deinit(void);

// Called from every button press and every window that comes to the front.
// Deliberately NOT called when the phone sends data: the app should close
// because the user stopped using it, not stay open because the hub is chatty.
void idle_poke(void);
