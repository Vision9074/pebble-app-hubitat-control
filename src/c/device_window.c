#include "device_window.h"

#include "comm.h"
#include "devices.h"
#include "idle.h"
#include "theme.h"

// Held up/down moves the level in steps this far apart.
#define REPEAT_MS 130

// A held button would otherwise fire a request per step; the level is sent once
// the user stops moving it.
#define SEND_DELAY_MS 450

// How old the table's readings must be before opening a device re-reads it.
#define STATE_FRESH_SECONDS 20

static Window *s_window;
static Layer *s_layer;
static int32_t s_device_id;

static AppTimer *s_send_timer;
static bool s_level_dirty;

static GPath *s_tri_up;
static GPath *s_tri_down;

static const GPathInfo TRI_UP_INFO = {
  .num_points = 3,
  .points = (GPoint[]){{-4, 3}, {4, 3}, {0, -4}}
};

static const GPathInfo TRI_DOWN_INFO = {
  .num_points = 3,
  .points = (GPoint[]){{-4, -3}, {4, -3}, {0, 4}}
};

static Device *current(void) {
  return device_by_id(s_device_id);
}

// ------------------------------------------------------------------ level ---

static void send_level(void *data) {
  s_send_timer = NULL;
  s_level_dirty = false;

  Device *d = current();
  if (d)
    comm_send_command(d->id, "level", d->level);
}

static void schedule_level_send(void) {
  s_level_dirty = true;
  if (s_send_timer)
    app_timer_reschedule(s_send_timer, SEND_DELAY_MS);
  else
    s_send_timer = app_timer_register(SEND_DELAY_MS, send_level, NULL);
}

static void adjust_level(Device *d, int delta) {
  int level = (d->level == LEVEL_NONE) ? 0 : d->level;
  level += delta;
  if (level < 0)
    level = 0;
  if (level > 100)
    level = 100;

  if ((uint8_t)level == d->level)
    return;

  d->level = (uint8_t)level;
  // Hubitat turns a dimmer on when it is given a level, so the local view
  // follows suit rather than waiting for the hub to answer.
  d->value = level > 0 ? device_on_value(d) : device_off_value(d);

  schedule_level_send();
  layer_mark_dirty(s_layer);
}

// ----------------------------------------------------------------- clicks ---

static void set_switched(Device *d, bool on) {
  comm_send_command(d->id, on ? "on" : "off", 0);
  d->value = on ? device_on_value(d) : device_off_value(d);
  layer_mark_dirty(s_layer);
}

static void select_click(ClickRecognizerRef recognizer, void *context) {
  idle_poke();

  Device *d = current();
  if (!d)
    return;

  if (d->kind == KindSensor) {
    comm_request_device(d->id);
    return;
  }

  if (d->kind == KindButton) {
    comm_send_command(d->id, "push", 0);
    vibes_short_pulse();
    return;
  }

  set_switched(d, !device_is_on(d));
}

static void up_click(ClickRecognizerRef recognizer, void *context) {
  idle_poke();

  Device *d = current();
  if (!d)
    return;

  if (device_has_level(d))
    adjust_level(d, comm_step());
  else if (device_is_controllable(d) && d->kind != KindButton)
    set_switched(d, true);
}

static void down_click(ClickRecognizerRef recognizer, void *context) {
  idle_poke();

  Device *d = current();
  if (!d)
    return;

  if (device_has_level(d))
    adjust_level(d, -comm_step());
  else if (device_is_controllable(d) && d->kind != KindButton)
    set_switched(d, false);
}

static void click_config(void *context) {
  window_single_click_subscribe(BUTTON_ID_SELECT, select_click);
  window_single_repeating_click_subscribe(BUTTON_ID_UP, REPEAT_MS, up_click);
  window_single_repeating_click_subscribe(BUTTON_ID_DOWN, REPEAT_MS, down_click);
}

// ------------------------------------------------------------------- draw ---

// On a round screen the hint column has to follow the glass in from the edge,
// and by more on a larger circle - at the height of the up and down marks a
// 260 px display has already curved ~13 px further in than a 180 px one.
static int hint_column_x(GRect bounds) {
  return PBL_IF_ROUND_ELSE(bounds.size.w - bounds.size.w / 12 - 6,
                           bounds.size.w - 11);
}

static void draw_hints(GContext *ctx, GRect bounds, const Device *d) {
  const int x = hint_column_x(bounds);
  const bool levelled = device_has_level(d);
  const bool switchable = device_is_controllable(d) && d->kind != KindButton;

  graphics_context_set_fill_color(ctx, THEME_ACCENT);

  if (levelled || switchable) {
    gpath_move_to(s_tri_up, GPoint(x, bounds.size.h * 28 / 100));
    gpath_draw_filled(ctx, s_tri_up);
    gpath_move_to(s_tri_down, GPoint(x, bounds.size.h * 72 / 100));
    gpath_draw_filled(ctx, s_tri_down);
  }

  graphics_fill_circle(ctx, GPoint(x, bounds.size.h / 2), 4);
}

// Hubitat keeps a dimmer's level when it is switched off, so the bar still has
// a length to show. Draining its colour keeps the headline word and the bar
// telling the same story.
static void draw_level_bar(GContext *ctx, GRect frame, int level, bool on) {
  graphics_context_set_stroke_color(ctx, THEME_DIM);
  graphics_draw_round_rect(ctx, frame, 3);

  if (level <= 0)
    return;

#if !defined(PBL_COLOR)
  // Black and white has no dimmed tone to fall back on, so an off device gets
  // a hollow bar instead. The percentage underneath still reports the level.
  if (!on)
    return;
#endif

  GRect fill = grect_inset(frame, GEdgeInsets(2));
  fill.size.w = fill.size.w * level / 100;
  if (fill.size.w < 2)
    fill.size.w = 2;

  graphics_context_set_fill_color(ctx, on ? THEME_ON : THEME_OFF);
  graphics_fill_rect(ctx, fill, 2, GCornersAll);
}

static void update_proc(Layer *layer, GContext *ctx) {
  const GRect bounds = layer_get_bounds(layer);
  const ScreenTier tier = theme_tier(bounds);

  graphics_context_set_fill_color(ctx, THEME_BACKGROUND);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);

  const Device *d = current();
  if (!d) {
    graphics_context_set_text_color(ctx, THEME_DIM);
    graphics_draw_text(ctx, "Device gone",
                       fonts_get_system_font(FONT_KEY_GOTHIC_18),
                       grect_inset(bounds, GEdgeInsets(bounds.size.h / 3, 8)),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter,
                       NULL);
    return;
  }

  draw_hints(ctx, bounds, d);

  // Insets scale with the screen so the round platforms stay inside the glass:
  // fixed values tuned for chalk leave content off the edge of a 260 px circle.
  const GRect content = grect_inset(bounds, GEdgeInsets(
      PBL_IF_ROUND_ELSE(bounds.size.h / 8, 8),   // top
      PBL_IF_ROUND_ELSE(bounds.size.w / 7, 20),  // right, clear of the hints
      PBL_IF_ROUND_ELSE(bounds.size.h / 10, 6),  // bottom
      PBL_IF_ROUND_ELSE(bounds.size.w / 7, 6))); // left

  // The name and the footer sit where a circle is narrowest, so on round they
  // get pulled in further than the value and the bar, which sit mid-screen.
  const GRect narrow = grect_inset(content, GEdgeInsets(
      0, PBL_IF_ROUND_ELSE(bounds.size.w / 12, 0),
      0, PBL_IF_ROUND_ELSE(bounds.size.w / 12, 0)));

  GFont name_font = fonts_get_system_font(
      tier == ScreenLarge ? FONT_KEY_GOTHIC_28_BOLD :
      tier == ScreenMedium ? FONT_KEY_GOTHIC_24_BOLD : FONT_KEY_GOTHIC_18_BOLD);
  GFont value_font = fonts_get_system_font(
      tier == ScreenLarge ? FONT_KEY_BITHAM_30_BLACK :
      tier == ScreenMedium ? FONT_KEY_GOTHIC_28_BOLD : FONT_KEY_GOTHIC_24_BOLD);
  GFont small_font = fonts_get_system_font(
      tier == ScreenSmall ? FONT_KEY_GOTHIC_14 : FONT_KEY_GOTHIC_18);

  const int name_line = tier == ScreenLarge ? 30 : tier == ScreenMedium ? 25 : 20;
  const int value_line = tier == ScreenLarge ? 36 : tier == ScreenMedium ? 32 : 27;
  const int small_line = tier == ScreenSmall ? 18 : 22;

  int y = content.origin.y;

  graphics_context_set_text_color(ctx, THEME_TEXT);
  graphics_draw_text(ctx, d->name, name_font,
                     GRect(narrow.origin.x, y, narrow.size.w, name_line * 2),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter,
                     NULL);
  y += name_line * 2 + 2;

  char value[16];
  device_value_text(d, value, sizeof(value));
  graphics_context_set_text_color(ctx, device_is_on(d) ? THEME_ON : THEME_OFF);
  graphics_draw_text(ctx, value, value_font,
                     GRect(content.origin.x, y, content.size.w, value_line),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter,
                     NULL);
  y += value_line + 6;

  if (device_has_level(d)) {
    const int level = (d->level == LEVEL_NONE) ? 0 : d->level;
    const int bar_h = tier == ScreenSmall ? 11 : 14;

    draw_level_bar(ctx, GRect(content.origin.x, y, content.size.w, bar_h),
                   level, device_is_on(d));
    y += bar_h + 2;

    char pct[8];
    snprintf(pct, sizeof(pct), "%d%%", level);
    graphics_context_set_text_color(ctx, THEME_TEXT);
    graphics_draw_text(ctx, pct, small_font,
                       GRect(content.origin.x, y, content.size.w, small_line),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter,
                       NULL);
    y += small_line;
  }

  // A sensor's reading is already the headline; anything else names its type so
  // the button mapping is not a guess. It sits on the bottom edge, but only
  // where there is still room for it: a round 180 px screen showing a level bar
  // has none, and the bar has already said "dimmer" more plainly than a word.
  const int footer_y = content.origin.y + content.size.h - small_line;
  if (footer_y >= y + 2) {
    const char *footer = d->kind == KindSensor ? "Select refreshes"
                                               : device_kind_name(d);
    graphics_context_set_text_color(ctx, THEME_DIM);
    graphics_draw_text(ctx, footer, small_font,
                       GRect(narrow.origin.x, footer_y, narrow.size.w, small_line),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter,
                       NULL);
  }
}

// ----------------------------------------------------------------- window ---

static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  const GRect bounds = layer_get_bounds(root);

  s_tri_up = gpath_create(&TRI_UP_INFO);
  s_tri_down = gpath_create(&TRI_DOWN_INFO);

  s_layer = layer_create(bounds);
  layer_set_update_proc(s_layer, update_proc);
  layer_add_child(root, s_layer);

  idle_poke();

  // The launch refresh usually landed moments ago, so re-reading this device
  // over the phone's radio would buy nothing. Only bother once it is stale.
  const int age = devices_state_age();
  if (age < 0 || age > STATE_FRESH_SECONDS)
    comm_request_device(s_device_id);
}

static void window_unload(Window *window) {
  // A level nudged but not yet sent would otherwise be lost on the way out.
  if (s_level_dirty) {
    if (s_send_timer) {
      app_timer_cancel(s_send_timer);
      s_send_timer = NULL;
    }
    send_level(NULL);
  }

  layer_destroy(s_layer);
  s_layer = NULL;
  gpath_destroy(s_tri_up);
  gpath_destroy(s_tri_down);
  s_tri_up = NULL;
  s_tri_down = NULL;
}

// The Window itself outlives each visit - destroying it from inside its own
// unload handler is not safe. main.c tears it down at exit instead.
void device_window_push(int32_t device_id) {
  s_device_id = device_id;
  s_level_dirty = false;

  if (!s_window) {
    s_window = window_create();
    window_set_background_color(s_window, THEME_BACKGROUND);
    window_set_click_config_provider(s_window, click_config);
    window_set_window_handlers(s_window, (WindowHandlers){
      .load = window_load,
      .unload = window_unload
    });
  }

  window_stack_push(s_window, true);
}

void device_window_deinit(void) {
  if (s_window) {
    window_destroy(s_window);
    s_window = NULL;
  }
}

void device_window_states_updated(void) {
  // A pending local change is newer than anything the hub just reported.
  if (s_layer && !s_level_dirty)
    layer_mark_dirty(s_layer);
}
