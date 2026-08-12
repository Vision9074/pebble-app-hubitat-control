#include "main_menu.h"

#include "comm.h"
#include "device_list.h"
#include "devices.h"
#include "idle.h"
#include "theme.h"

#define ROW_DEVICES 0
#define ROW_REFRESH 1
#define ROW_MODE 2
#define ROW_STATUS 3
#define ROW_COUNT 4

static Window *s_window;
static MenuLayer *s_menu;

static uint16_t get_num_rows(MenuLayer *menu, uint16_t section, void *context) {
  return ROW_COUNT;
}

static int16_t get_cell_height(MenuLayer *menu, MenuIndex *index, void *context) {
  return PBL_IF_ROUND_ELSE(
      menu_layer_is_index_selected(menu, index) ? MENU_CELL_ROUND_FOCUSED_TALL_CELL_HEIGHT
                                                : MENU_CELL_ROUND_UNFOCUSED_TALL_CELL_HEIGHT,
      44);
}

static void draw_row(GContext *ctx, const Layer *cell_layer, MenuIndex *index,
                     void *context) {
  char subtitle[32];

  switch (index->row) {
    case ROW_DEVICES: {
      const int n = devices_count();
      if (n == 0)
        snprintf(subtitle, sizeof(subtitle), "%s", comm_status_text());
      else
        snprintf(subtitle, sizeof(subtitle), "%d device%s", n, n == 1 ? "" : "s");
      menu_cell_basic_draw(ctx, cell_layer, "Devices", subtitle, NULL);
      break;
    }

    case ROW_REFRESH:
      menu_cell_basic_draw(ctx, cell_layer, "Refresh Devices",
                           "Re-read the hub", NULL);
      break;

    case ROW_MODE:
      if (comm_mode() == MODE_CLOUD)
        snprintf(subtitle, sizeof(subtitle), "Cloud");
      else if (comm_cloud_available())
        snprintf(subtitle, sizeof(subtitle), "Local");
      else
        // No cloud route configured, so there is nothing to switch to.
        snprintf(subtitle, sizeof(subtitle), "Local only");
      menu_cell_basic_draw(ctx, cell_layer, "Connection", subtitle, NULL);
      break;

    case ROW_STATUS:
      menu_cell_basic_draw(ctx, cell_layer, "Status", comm_status_text(), NULL);
      break;

    default:
      break;
  }
}

static void selection_changed(struct MenuLayer *menu, MenuIndex new_index,
                              MenuIndex old_index, void *context) {
  idle_poke();
}

static void select_click(MenuLayer *menu, MenuIndex *index, void *context) {
  idle_poke();

  switch (index->row) {
    case ROW_DEVICES:
      device_list_push();
      break;

    case ROW_REFRESH:
      // Discards the phone's cached list, so devices added on the hub since the
      // last fetch appear.
      comm_request_list(true);
      layer_mark_dirty(menu_layer_get_layer(menu));
      break;

    case ROW_MODE:
      // Nothing to toggle to when the hub has no cloud route.
      if (comm_mode() == MODE_LOCAL && !comm_cloud_available())
        break;
      comm_set_mode(comm_mode() == MODE_CLOUD ? MODE_LOCAL : MODE_CLOUD);
      // The list itself is unchanged; only the route to the hub moved.
      comm_request_states();
      layer_mark_dirty(menu_layer_get_layer(menu));
      break;

    case ROW_STATUS:
      comm_request_states();
      break;

    default:
      break;
  }
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

// Coming back from a device is activity too, and this catches the Back button
// without having to take over the menu's own click handling.
static void window_appear(Window *window) {
  idle_poke();
}

void main_menu_push(void) {
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

void main_menu_reload(void) {
  if (s_menu)
    layer_mark_dirty(menu_layer_get_layer(s_menu));
}

void main_menu_deinit(void) {
  if (s_window) {
    window_destroy(s_window);
    s_window = NULL;
  }
}
