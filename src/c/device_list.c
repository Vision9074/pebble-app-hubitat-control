#include "device_list.h"

#include "comm.h"
#include "device_window.h"
#include "devices.h"
#include "idle.h"
#include "theme.h"

static Window *s_window;
static MenuLayer *s_menu;

// An empty table still needs one row to explain itself.
static bool is_empty(void) {
  return devices_count() == 0;
}

static uint16_t get_num_rows(MenuLayer *menu, uint16_t section, void *context) {
  return is_empty() ? 1 : devices_count();
}

static int16_t get_cell_height(MenuLayer *menu, MenuIndex *index, void *context) {
  return PBL_IF_ROUND_ELSE(
      menu_layer_is_index_selected(menu, index) ? MENU_CELL_ROUND_FOCUSED_TALL_CELL_HEIGHT
                                                : MENU_CELL_ROUND_UNFOCUSED_TALL_CELL_HEIGHT,
      44);
}

static void draw_row(GContext *ctx, const Layer *cell_layer, MenuIndex *index,
                     void *context) {
  if (is_empty()) {
    const char *title = comm_link_state() == LinkError ? "Not available"
                                                       : "Loading...";
    menu_cell_basic_draw(ctx, cell_layer, title, comm_status_text(), NULL);
    return;
  }

  const Device *d = device_at(index->row);
  if (!d)
    return;

  char status[24];
  device_status_text(d, status, sizeof(status));
  menu_cell_basic_draw(ctx, cell_layer, d->name, status, NULL);
}

static void selection_changed(struct MenuLayer *menu, MenuIndex new_index,
                              MenuIndex old_index, void *context) {
  idle_poke();
}

static void select_click(MenuLayer *menu, MenuIndex *index, void *context) {
  idle_poke();

  const Device *d = device_at(index->row);
  if (d)
    device_window_push(d->id);
}

// A long press is the shortcut for the common case: flip a light without
// opening its screen. Anything with a level still opens, since a level needs
// the up/down buttons that the list is using to scroll.
static void select_long_click(MenuLayer *menu, MenuIndex *index, void *context) {
  idle_poke();

  Device *d = device_at(index->row);
  if (!d)
    return;

  if (!device_is_controllable(d)) {
    comm_request_device(d->id);
    return;
  }

  // Unlocking a door or opening a garage is the one thing here with a physical
  // consequence, and a long press is easy to make by accident. Unless the user
  // has opted in, send them to the device instead of acting - the press still
  // does something useful, just nothing irreversible.
  if ((d->kind == KindLock || d->kind == KindDoor) &&
      !comm_quick_lock_allowed()) {
    device_window_push(d->id);
    return;
  }

  if (d->kind == KindButton) {
    comm_send_command(d->id, "push", 0);
  } else {
    const bool on = device_is_on(d);
    comm_send_command(d->id, on ? "off" : "on", 0);
    d->value = on ? device_off_value(d) : device_on_value(d);
  }

  vibes_short_pulse();
  layer_mark_dirty(menu_layer_get_layer(menu));
}

static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  const GRect bounds = layer_get_bounds(root);

  s_menu = menu_layer_create(bounds);
  menu_layer_set_callbacks(s_menu, NULL, (MenuLayerCallbacks){
    .get_num_rows = get_num_rows,
    .get_cell_height = get_cell_height,
    .draw_row = draw_row,
    .select_click = select_click,
    .select_long_click = select_long_click,
    .selection_changed = selection_changed
  });
  menu_layer_set_click_config_onto_window(s_menu, window);
  menu_layer_set_normal_colors(s_menu, THEME_MENU_BG, THEME_MENU_FG);
  menu_layer_set_highlight_colors(s_menu, THEME_MENU_SEL_BG, THEME_MENU_SEL_FG);
#if defined(PBL_ROUND)
  menu_layer_set_center_focused(s_menu, true);
#endif
  layer_add_child(root, menu_layer_get_layer(s_menu));
}

static void window_unload(Window *window) {
  menu_layer_destroy(s_menu);
  s_menu = NULL;
}

static void window_appear(Window *window) {
  idle_poke();
}

void device_list_push(void) {
  if (!s_window) {
    s_window = window_create();
    window_set_background_color(s_window, THEME_MENU_BG);
    window_set_window_handlers(s_window, (WindowHandlers){
      .load = window_load,
      .appear = window_appear,
      .unload = window_unload
    });
  }

  window_stack_push(s_window, true);
}

void device_list_reload(void) {
  if (!s_menu)
    return;

  // reload_data keeps the selection where it is when the row count is stable,
  // which matters while state chunks trickle in.
  menu_layer_reload_data(s_menu);
}

void device_list_deinit(void) {
  if (s_window) {
    window_destroy(s_window);
    s_window = NULL;
  }
}
