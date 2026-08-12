#include "idle.h"

#define IDLE_TIMEOUT_MS (60 * 1000)

static AppTimer *s_timer;

static void expire(void *data) {
  s_timer = NULL;
  // Emptying the window stack ends the event loop, which is how a Pebble app
  // bows out and hands the screen back to the watchface.
  window_stack_pop_all(true);
}

void idle_poke(void) {
  // Rescheduling fails once a timer has already fired, so fall back to a new
  // one rather than assuming the handle is still good.
  if (s_timer && app_timer_reschedule(s_timer, IDLE_TIMEOUT_MS))
    return;

  s_timer = app_timer_register(IDLE_TIMEOUT_MS, expire, NULL);
}

void idle_init(void) {
  idle_poke();
}

void idle_deinit(void) {
  if (s_timer) {
    app_timer_cancel(s_timer);
    s_timer = NULL;
  }
}
