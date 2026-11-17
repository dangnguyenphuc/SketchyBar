#include "bar.h"
#include "bar_manager.h"
#include "event.h"
#include "display.h"
#include "misc/helpers.h"
#include "window.h"
#include "mouse.h"
#include "stats.h"
#include "password_field.h"

extern int workspace_display_notch_height(uint32_t did);
extern int workspace_display_notch_width(uint32_t did);

bool bar_draws_item(struct bar* bar, struct bar_item* bar_item) {
    if (!bar_item->drawing || !bar->shown || bar->hidden) return false;

    if (((bar_item->associated_display > 0
          && (!(bar_item->associated_display & (1 << bar->adid))))
        || (bar_item->associated_to_active_display
            && (bar->adid != g_bar_manager.active_adid)))
        && !bar_item->ignore_association)
      return false;

    if (bar_item->associated_space > 0
        && (!(bar_item->associated_space & (1 << bar->sid))
            && !bar_item->ignore_association)
        && (bar_item->type != BAR_COMPONENT_SPACE)        )
      return false;

    if (bar_item->position == POSITION_POPUP
        && (!bar_item->parent
            || !bar_item->parent->popup.drawing
            || (bar->adid != g_bar_manager.active_adid)))
      return false;

    return true;
}

static void bar_calculate_popup_anchor_for_bar_item(struct bar* bar, struct bar_item* bar_item) {
  if (bar->adid != g_bar_manager.active_adid) return;
  struct window* window = bar_item_get_window(bar_item, bar->adid);

  if (!bar_item->popup.overrides_cell_size) {
    if (g_bar_manager.position == POSITION_LEFT
        || g_bar_manager.position == POSITION_RIGHT) {
      bar_item->popup.cell_size = window->frame.size.width;
    } else {
      bar_item->popup.cell_size = window->frame.size.height;
    }
  }

  popup_calculate_bounds(&bar_item->popup, bar);

  CGPoint anchor = window->origin;

  if (g_bar_manager.position == POSITION_LEFT
      || g_bar_manager.position == POSITION_RIGHT) {
    if (bar_item->popup.align == POSITION_CENTER) {
      anchor.y += (window->frame.size.height
                   - bar_item->popup.background.bounds.size.height) / 2;
    } else if (bar_item->popup.align == POSITION_LEFT) {
      anchor.y -= bar_item->background.padding_left;
    } else {
      anchor.y += window->frame.size.height
                  - bar_item->popup.background.bounds.size.height;
    }
    anchor.x += (g_bar_manager.position == POSITION_RIGHT
                ? (- bar_item->popup.background.bounds.size.width)
                : window->frame.size.width);
  } else {
    if (bar_item->popup.align == POSITION_CENTER) {
      anchor.x += (window->frame.size.width
                   - bar_item->popup.background.bounds.size.width) / 2;
    } else if (bar_item->popup.align == POSITION_LEFT) {
      anchor.x -= bar_item->background.padding_left;
    } else {
      anchor.x += window->frame.size.width
                  - bar_item->popup.background.bounds.size.width;
    }
    anchor.y += (g_bar_manager.position == POSITION_BOTTOM
                ? (- bar_item->popup.background.bounds.size.height)
                : window->frame.size.height);
  }
  popup_set_anchor(&bar_item->popup, anchor, bar->adid);
  popup_calculate_bounds(&bar_item->popup, bar);
}

void bar_order_item_windows(struct bar* bar) {
  if (bar->sid < 1 || bar->adid < 1 || !bar->shown) return;
  window_set_level(&bar->window, g_bar_manager.window_level);
  window_order(&bar->window, NULL, W_ABOVE);

  struct window* previous_window = NULL;
  struct window* first_window = NULL;
  struct window* chain[g_bar_manager.bar_item_count + 1];
  struct window* items[g_bar_manager.bar_item_count + 1];
  int group_count = 0, item_count = 0;
  for (int i = 0; i < g_bar_manager.bar_item_count; i++) {
    struct bar_item* bar_item = g_bar_manager.bar_items[i];
    if (bar_item->position == POSITION_POPUP) continue;

    struct window* window = bar_item_get_window(bar_item, bar->adid);
    window_set_level(window, g_bar_manager.window_level);

    if (!first_window) first_window = window;

    if (bar_item->type == BAR_COMPONENT_GROUP) {
      chain[group_count++] = window;
      if (first_window)
        window_order(window, first_window, W_BELOW);
      else
        window_order(window, &bar->window, W_ABOVE);
      continue;
    }

    items[item_count++] = window;
    if (previous_window) window_order(window, previous_window, W_ABOVE);
    else window_order(window, &bar->window, W_ABOVE);

    previous_window = window;
  }

  // Brackets sit between the bar and the items.
  memcpy(chain + group_count, items, sizeof(struct window*) * item_count);
  windows_set_order_chain(&bar->window, chain, group_count + item_count);
}

static void bar_check_for_clip_updates(struct bar* bar) {
  if (!g_bar_manager.bar_needs_update) {
    for (int i = 0; i < g_bar_manager.bar_item_count; i++) {
      struct bar_item* bar_item = g_bar_manager.bar_items[i];
      struct window* window = bar_item_get_window(bar_item, bar->adid);

      bool clips_bar = bar_item_clips_bar(bar_item);
      if (!clips_bar || (!bar_draws_item(bar, bar_item))) {

        if (clips_bar && !CGPointEqualToPoint(window->origin, g_nirvana)) {
          g_bar_manager.bar_needs_update = true;
          break;
        }

        continue;
      }

      g_bar_manager.bar_needs_update
                       |= window->needs_move
                          || window->needs_resize
                          || bar_item_clip_needs_update_for_bar(bar_item, bar);

      if (g_bar_manager.bar_needs_update) break;
    }
  }
}

static bool bar_is_horizontal() {
  return g_bar_manager.position != POSITION_LEFT
         && g_bar_manager.position != POSITION_RIGHT;
}

static double bar_collapsed_height(struct bar* bar) {
  if (workspace_display_notch_height(bar->did) > 0
      && g_bar_manager.notch_display_height > 0  )
    return g_bar_manager.notch_display_height;
  return g_bar_manager.background.bounds.size.height;
}

static double bar_progress_fraction(int progress) {
  double t = (double)progress / BAR_EXPAND_PROGRESS_MAX;
  return t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t);
}

// Size of the visible notch shape for an expand progress and a widen
// progress (Wi-Fi prompt).
static CGSize bar_get_shape_size_wide(struct bar* bar,
                                      int progress,
                                      int wide_progress) {
  double t = bar_progress_fraction(progress);
  double expanded_width = BAR_NOTCH_EXPANDED_WIDTH
                          + (BAR_NOTCH_WIDE_WIDTH - BAR_NOTCH_EXPANDED_WIDTH)
                            * bar_progress_fraction(wide_progress);

  double collapsed_height = bar_collapsed_height(bar);
  return (CGSize){
    round(BAR_NOTCH_COLLAPSED_WIDTH
          + (expanded_width - BAR_NOTCH_COLLAPSED_WIDTH) * t),
    round(collapsed_height
          + (BAR_NOTCH_EXPANDED_HEIGHT - collapsed_height) * t)
  };
}

static CGSize bar_get_shape_size(struct bar* bar, int progress) {
  return bar_get_shape_size_wide(bar, progress, bar->wide_progress);
}

// Like the expand animation, widening keeps the window at the wide size for
// the whole animation and only changes the drawn shape.
static bool bar_uses_wide_window(struct bar* bar) {
  return bar->wide || bar->wide_progress > 0;
}

// While expanded or animating, the window keeps the full expanded size and
// only the drawn shape changes. Resizing the window on every animation frame
// is not atomic with the redraw, which made the shape jitter on the y-axis.
static bool bar_uses_expanded_window(struct bar* bar) {
  return bar->expanded || bar->expand_progress > 0;
}

// Rect of the visible shape inside the window (CG coordinates, y-up). It is
// centered horizontally and pinned to the screen edge the bar is attached to,
// so its top (or bottom) edge never moves during the animation.
static CGRect bar_get_shape_bounds(struct bar* bar) {
  CGRect frame = bar->window.frame;
  if (!bar_is_horizontal()) return frame;

  CGSize size = bar_get_shape_size(bar, bar->expand_progress);
  if (size.width > frame.size.width) size.width = frame.size.width;
  if (size.height > frame.size.height) size.height = frame.size.height;

  frame.origin.x = round((frame.size.width - size.width) / 2.0);
  frame.origin.y = g_bar_manager.position == POSITION_BOTTOM
                   ? 0
                   : frame.size.height - size.height;
  frame.size = size;
  return frame;
}

// The fully expanded shape at the current width (window-local, y-up): what
// the content is laid out in. It stays put during the expand animation
// (the shape only clips it) and follows the widening for the Wi-Fi prompt.
static CGRect bar_get_layout_bounds(struct bar* bar) {
  CGRect frame = bar->window.frame;
  CGSize size = bar_get_shape_size(bar, BAR_EXPAND_PROGRESS_MAX);
  if (size.width > frame.size.width) size.width = frame.size.width;
  if (size.height > frame.size.height) size.height = frame.size.height;

  return (CGRect){{ round((frame.size.width - size.width) / 2.0),
                    g_bar_manager.position == POSITION_BOTTOM
                    ? 0
                    : frame.size.height - size.height          },
                  size                                           };
}

// Area of the expanded island below the notch band (window-local, y-up).
static CGRect bar_get_island_panel(struct bar* bar) {
  CGRect bounds = bar_get_layout_bounds(bar);
  double band = bar_collapsed_height(bar);
  if (g_bar_manager.position == POSITION_BOTTOM) bounds.origin.y += band;
  bounds.size.height -= band;
  return bounds;
}

// The parts of the notch band left and right of the camera housing
// (window-local, y-up). Nothing can be seen in between, so only these two are
// drawn into.
static void bar_get_band_sides(struct bar* bar, CGRect* left, CGRect* right) {
  CGRect bounds = bar_get_layout_bounds(bar);
  double band = bar_collapsed_height(bar);
  double notch = bar->notch_width > 0 ? bar->notch_width
                                      : g_bar_manager.notch_width;
  double side = max(floor((bounds.size.width - notch) / 2.0), 0);
  double y = g_bar_manager.position == POSITION_BOTTOM
             ? bounds.origin.y
             : CGRectGetMaxY(bounds) - band;

  *left = (CGRect){{ bounds.origin.x, y }, { side, band }};
  *right = (CGRect){{ CGRectGetMaxX(bounds) - side, y }, { side, band }};
}

// Last battery/CPU/RAM sample, shared by all bars (same machine).
static struct system_stats g_island_stats = { -1, -1, -1 };
static struct system_stats_history g_island_history;

static struct island_data bar_island_data() {
  return (struct island_data){ .stats = &g_island_stats,
                               .history = &g_island_history,
                               .media = now_playing_get(),
                               .audio = audio_output_get(),
                               .info = system_info_get(),
                               .connections = connections_get() };
}

static void bar_island_open_prompt(struct bar* bar, const char* ssid);
static void bar_island_close_prompt(struct bar* bar);
static void bar_island_update_sources();
static void bar_island_update_desktops(struct bar* bar);
static void bar_island_update_brightness(struct bar* bar);
static void bar_click_toggle(struct bar* bar, int column);
static void bar_sync_toggles(struct bar* bar);
static void bar_island_connections_changed(void);
static void bar_island_join(struct bar* bar, const char* password);

// Runs what an input event on the island content asked for.
static void bar_island_perform(struct bar* bar, struct island_action* action) {
  const struct connections* connections = connections_get();
  switch (action->type) {
    case ISLAND_ACTION_WIFI_POWER:
      bar_click_toggle(bar, 0);
      break;
    case ISLAND_ACTION_BLUETOOTH_POWER:
      bar_click_toggle(bar, 1);
      break;
    case ISLAND_ACTION_WIFI_SELECT: {
      if (action->index < 0 || action->index >= connections->wifi_count
          || connections->status == CONNECTION_CONNECTING              ) {
        break;
      }
      struct wifi_network network = connections->wifi[action->index];
      if (network.current) break;

      // New secured networks (and saved ones that failed) ask for the
      // password first; open and saved ones are joined right away.
      if (connections_wifi_needs_password(&network))
        bar_island_open_prompt(bar, network.ssid);
      else
        connections_join_wifi(network.ssid, NULL);
      break;
    }
    case ISLAND_ACTION_BLUETOOTH_SELECT:
      if (action->index < 0 || action->index >= connections->bluetooth_count
          || connections->status == CONNECTION_CONNECTING                   ) {
        break;
      }
      connections_toggle_bluetooth(connections->bluetooth[action->index]
                                                .address              );
      break;
    case ISLAND_ACTION_PROMPT_JOIN:
      // The field calls back with its text (bar_island_password_done).
      if (password_field_visible()) password_field_submit();
      else bar_island_join(bar, connections_wifi_password(
                                  bar->island.prompt_ssid ));
      break;
    case ISLAND_ACTION_PROMPT_CANCEL:
      bar_island_close_prompt(bar);
      break;
    case ISLAND_ACTION_SCREEN_SELECT:
      // Desktops that don't exist, or the current one, do nothing. The
      // highlight follows once the space actually changed.
      if (action->index < bar->desktops.count
          && action->index != bar->desktops.active) {
        desktops_switch(action->index);
      }
      // Access may have just been asked for or granted.
      bar_island_update_desktops(bar);
      break;
    case ISLAND_ACTION_PREVIOUS:
      now_playing_send(NOW_PLAYING_PREVIOUS);
      break;
    case ISLAND_ACTION_PLAY_PAUSE:
      now_playing_send(NOW_PLAYING_PLAY_PAUSE);
      break;
    case ISLAND_ACTION_NEXT:
      now_playing_send(NOW_PLAYING_NEXT);
      break;
    case ISLAND_ACTION_TOGGLE_MUTE:
      audio_output_set_muted(!audio_output_get()->muted);
      break;
    case ISLAND_ACTION_SET_VOLUME:
      audio_output_set_volume(action->value);
      break;
    case ISLAND_ACTION_SET_BRIGHTNESS:
      if (bar->has_brightness) {
        DisplayServicesSetBrightness(bar->did, action->value);
        bar->brightness = action->value;
      }
      break;
    case ISLAND_ACTION_NONE:
      break;
  }
}

// Converts a global (y-down) event location into window-local (y-up) coords.
static CGPoint bar_get_local_point(struct bar* bar, CGPoint point) {
  return (CGPoint){ point.x - bar->window.origin.x,
                    bar->window.frame.size.height
                    - (point.y - bar->window.origin.y) };
}

static bool bar_shows_island(struct bar* bar) {
  return bar_is_horizontal() && bar->expanded && bar->expand_progress > 0;
}

// One frame of the mouse wheel glide: moves scroll_offset a time-based
// fraction of the way to scroll_target. Called by a "driver" animation that
// only supplies display frames; its interpolated value is ignored. So a new
// wheel tick just moves the target and restarts the driver, and the glide
// continues seamlessly instead of starting over from a standstill.
static bool bar_step_island_scroll(void* target, int value) {
  struct bar* bar = target;
  struct island* island = &bar->island;

  uint64_t now = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
  double dt = (double)(now - bar->scroll_glide_time) / 1e9;
  // The first frame of a glide (or one after a stall) steps one 60Hz frame.
  if (!bar->scroll_glide_time || dt <= 0 || dt > 0.05) dt = 1.0 / 60.0;
  bar->scroll_glide_time = now;

  CGFloat remaining = island->scroll_target - island->scroll_offset;
  if (remaining == 0) return false;

  if (fabs(remaining) < 0.25) {
    island->scroll_offset = island->scroll_target;
  } else {
    island->scroll_offset += remaining
                             * (1.0 - exp(-dt / BAR_SCROLL_WHEEL_TAU));
  }
  return true;
}

static void bar_cancel_island_scroll(struct bar* bar) {
  animator_cancel_locked(&g_bar_manager.animator,
                         bar,
                         (animator_function*)bar_step_island_scroll);
  bar->scroll_glide_time = 0;
}

static void bar_glide_island_scroll(struct bar* bar) {
  // Restarting the driver keeps scroll_glide_time, so the step size stays
  // continuous across wheel ticks.
  uint64_t glide_time = bar->scroll_glide_time;
  bar_cancel_island_scroll(bar);
  bar->scroll_glide_time = glide_time;
  if (bar->island.scroll_offset == bar->island.scroll_target) return;

  bool needs_refresh = false;
  uint32_t saved_duration = g_bar_manager.animator.duration;
  char saved_interp = g_bar_manager.animator.interp_function;
  g_bar_manager.animator.duration = BAR_SCROLL_GLIDE_FRAMES;
  g_bar_manager.animator.interp_function = INTERP_FUNCTION_LINEAR;

  ANIMATE(bar_step_island_scroll, bar, 0, 1);
  animator_lock(&g_bar_manager.animator);

  g_bar_manager.animator.duration = saved_duration;
  g_bar_manager.animator.interp_function = saved_interp;
  (void)needs_refresh;
}

static bool bar_set_island_transition(void* target, int value) {
  struct bar* bar = target;
  double transition = (double)value / BAR_EXPAND_PROGRESS_MAX;
  if (bar->island.transition == transition) return false;
  bar->island.transition = transition;
  return true;
}

static void bar_cancel_island_transition(struct bar* bar) {
  animator_cancel_locked(&g_bar_manager.animator,
                         bar,
                         (animator_function*)bar_set_island_transition);
}

static void bar_animate_island_transition(struct bar* bar) {
  bar_cancel_island_transition(bar);

  bool needs_refresh = false;
  uint32_t saved_duration = g_bar_manager.animator.duration;
  char saved_interp = g_bar_manager.animator.interp_function;
  g_bar_manager.animator.duration = BAR_PAGE_DURATION;
  g_bar_manager.animator.interp_function = INTERP_FUNCTION_SIN;

  ANIMATE(bar_set_island_transition, bar, 0, BAR_EXPAND_PROGRESS_MAX);
  animator_lock(&g_bar_manager.animator);

  g_bar_manager.animator.duration = saved_duration;
  g_bar_manager.animator.interp_function = saved_interp;
  (void)needs_refresh;
}

bool bar_contains_point(struct bar* bar, CGPoint point) {
  CGRect frame = bar->window.frame;
  frame.origin = bar->window.origin;
  return CGRectContainsPoint(frame, point);
}

bool bar_island_click(struct bar* bar, CGPoint point) {
  if (!bar_shows_island(bar)) return false;
  bar_island_touch(bar);
  // A service switch resets the scroll position; a still running scroll
  // animation must not move it again.
  bar_cancel_island_scroll(bar);
  bar->island.scroll_target = bar->island.scroll_offset;

  int service = bar->island.service;
  struct island_action action;
  if (!island_handle_click(&bar->island,
                           bar_get_island_panel(bar),
                           bar_get_local_point(bar, point),
                           &action                         )) {
    return false;
  }
  bar_island_perform(bar, &action);

  if (bar->island.service != service) {
    // Leaving the Connections tab drops an open Wi-Fi prompt.
    if (service == ISLAND_SERVICE_CONNECTIONS) bar_island_close_prompt(bar);
    bar_animate_island_transition(bar);
    bar_island_update_sources();
    if (bar->island.service == ISLAND_SERVICE_CONNECTIONS)
      bar_sync_toggles(bar);
    if (bar->island.service == ISLAND_SERVICE_SCREENS)
      bar_island_update_desktops(bar);
    if (bar->island.service == ISLAND_SERVICE_SYSTEM)
      bar_island_update_brightness(bar);
  }
  g_bar_manager.bar_needs_update = true;
  return true;
}

// Reads the display's desktops for the Screens tab, and whether clicking
// one can switch to it.
static void bar_island_update_desktops(struct bar* bar) {
  desktops_read(bar->did, &bar->desktops);

  bool shortcuts = false;
  for (int i = 0; i < ISLAND_SCREEN_COUNT; i++) {
    if (desktops_shortcut_enabled(i)) shortcuts = true;
  }
  bar->desktops_problem = !shortcuts ? ISLAND_SCREENS_NO_SHORTCUTS
                          : !desktops_can_switch() ? ISLAND_SCREENS_NO_ACCESS
                          : ISLAND_SCREENS_OK;
  g_bar_manager.bar_needs_update = true;
}

// The display's brightness for the System tab's slider. Displays macOS can't
// dim (most external monitors) have none.
static void bar_island_update_brightness(struct bar* bar) {
  float brightness = 0;
  bar->has_brightness = DisplayServicesCanChangeBrightness(bar->did)
                        && DisplayServicesGetBrightness(bar->did,
                                                        &brightness)
                           == kCGErrorSuccess;
  bar->brightness = bar->has_brightness ? brightness : 0;
  g_bar_manager.bar_needs_update = true;
}

// The active space changed: move the Screens tab's highlight (only read
// while that tab is showing).
void bar_island_spaces_changed(struct bar* bar) {
  if (bar->expanded && bar->island.service == ISLAND_SERVICE_SCREENS)
    bar_island_update_desktops(bar);
}

bool bar_island_drag(struct bar* bar, CGPoint point) {
  if (!bar_shows_island(bar)) return false;

  struct island_action action;
  if (!island_handle_drag(&bar->island,
                          bar_get_island_panel(bar),
                          bar_get_local_point(bar, point),
                          &action                         )) {
    return false;
  }
  bar_island_touch(bar);
  bar_island_perform(bar, &action);
  g_bar_manager.bar_needs_update = true;
  return true;
}

// `delta` is in points (positive scrolls towards the top of the list).
bool bar_island_scroll(struct bar* bar,
                       CGPoint point,
                       CGFloat delta,
                       bool continuous) {
  if (!bar_shows_island(bar)) return false;
  bar_island_touch(bar);
  if (!island_handle_scroll(&bar->island,
                            bar_get_island_panel(bar),
                            bar_get_local_point(bar, point),
                            delta                           )) {
    return false;
  }

  if (continuous) {
    // Trackpad events already arrive at about the display rate: apply them
    // right away and redraw now (the caller refreshes), no added latency.
    bar_cancel_island_scroll(bar);
    if (bar->island.scroll_offset != bar->island.scroll_target) {
      bar->island.scroll_offset = bar->island.scroll_target;
      g_bar_manager.bar_needs_update = true;
    }
  } else {
    bar_glide_island_scroll(bar);
  }
  return true;
}

// Island content (fonts, notification data) is only kept while at least one
// island is open or animating.
static void bar_release_island_if_idle() {
  for (int i = 0; i < g_bar_manager.bar_count; i++) {
    struct bar* bar = g_bar_manager.bars[i];
    if (bar && (bar->expanded || bar->expand_progress > 0)) return;
  }
  island_release();
}

// The shape drawn into the window: the visible shape stretched past the
// screen edge the bar is attached to by the corner radius. Those corners then
// lie outside the window and get cut off, so only the free side is rounded,
// like the hardware notch.
static CGRect bar_get_drawn_shape(struct bar* bar) {
  CGRect shape = bar_get_shape_bounds(bar);
  if (!bar_is_horizontal()) return shape;

  double radius = g_bar_manager.background.corner_radius;
  shape.size.height += radius;
  if (g_bar_manager.position == POSITION_BOTTOM) shape.origin.y -= radius;
  return shape;
}

void bar_draw(struct bar* bar, bool forced) {
  if (bar->sid < 1 || bar->adid < 1) return;

  if (g_bar_manager.might_need_clipping)
    bar_check_for_clip_updates(bar);

  if (g_bar_manager.bar_needs_update) {
    struct background background = g_bar_manager.background;
    background.bounds = bar_get_drawn_shape(bar);
    background.bounds.origin.y -= background.y_offset;
    background.shadow.enabled = false;
    background.enabled = true;
    windows_freeze();
    CGContextClearRect(bar->window.surface->context, bar->window.frame);
    background_draw(&background, bar->window.surface->context);

    if (bar_is_horizontal() && bar->expand_progress > 0) {
      struct island_data data = bar_island_data();
      data.desktops = &bar->desktops;
      data.desktops_problem = bar->desktops_problem;
      data.has_brightness = bar->has_brightness;
      data.brightness = bar->brightness;
      island_draw(&bar->island,
                  bar->window.surface->context,
                  bar_get_island_panel(bar),
                  bar_get_drawn_shape(bar),
                  g_bar_manager.background.corner_radius,
                  &data,
                  (double)bar->expand_progress / BAR_EXPAND_PROGRESS_MAX);

      CGRect left, right;
      bar_get_band_sides(bar, &left, &right);
      island_draw_band(&bar->island,
                       bar->window.surface->context,
                       left,
                       right,
                       &data,
                       (double)bar->expand_progress
                       / BAR_EXPAND_PROGRESS_MAX     );
    }
  }

  for (int i = 0; i < g_bar_manager.bar_item_count; i++) {
    struct bar_item* bar_item = g_bar_manager.bar_items[i];
    struct window* window = bar_item_get_window(bar_item, bar->adid);

    if (!bar_draws_item(bar, bar_item)){
      if (!CGPointEqualToPoint(window->origin, g_nirvana)) {
        window_move(window, g_nirvana);
      }

      bar_item_remove_associated_bar(bar_item, bar->adid);
      continue;
    }

    bar_item_append_associated_bar(bar_item, bar->adid);

    if (bar_item->popup.drawing && bar->adid == g_bar_manager.active_adid)
      popup_draw(&bar_item->popup);

    if (g_bar_manager.bar_needs_update) {
      bar_item_clip_bar(bar_item,
                        window->origin.x - bar->window.origin.x,
                        bar                                     );
    }

    bool resized = window_apply_frame(window, forced);
    if (!resized && !bar_item->needs_update) continue;

    if (bar_item->update_mask & UPDATE_MOUSE_ENTERED
        || bar_item->update_mask & UPDATE_MOUSE_EXITED) {
      window_assign_mouse_tracking_area(window, window->frame);
    }

    CGContextClearRect(window->surface->context, window->frame);
    bar_item_draw(bar_item, window->surface->context);
    CGContextFlush(window->surface->context);
    if (!resized) window_flush(window);
  }

  if (g_bar_manager.bar_needs_update) {
    CGContextFlush(bar->window.surface->context);
    window_flush(&bar->window);
  }
}

static void bar_calculate_bounds_top_bottom(struct bar* bar) {
  bool has_notch = workspace_display_notch_height(bar->did) > 0;
  uint32_t notch_width = has_notch ? g_bar_manager.notch_width : 0;

  uint32_t center_length = bar_manager_length_for_bar_side(&g_bar_manager,
                                                           bar,
                                                           POSITION_CENTER);

  uint32_t bar_left_first_item_x = max(g_bar_manager.background.padding_left,
                                       0                                     );

  uint32_t bar_right_first_item_x = bar->window.frame.size.width
                                   -max(g_bar_manager.background.padding_right,
                                          0                                  );

  uint32_t bar_center_first_item_x = (bar->window.frame.size.width
                                      - center_length) / 2;

  uint32_t bar_center_right_first_item_x = (bar->window.frame.size.width
                                            + notch_width) / 2;

  uint32_t bar_center_left_first_item_x = (bar->window.frame.size.width
                                           - notch_width) / 2;

  uint32_t* next_position = NULL;
  uint32_t y = g_bar_manager.background.bounds.size.height / 2;

  for (int i = 0; i < g_bar_manager.bar_item_count; i++) {
    struct bar_item* bar_item = g_bar_manager.bar_items[i];

    if (!bar_draws_item(bar, bar_item)
        || bar_item->type == BAR_COMPONENT_GROUP
        || bar_item->position == POSITION_POPUP ) {
      continue;
    } 

    uint32_t bar_item_display_length = bar_item_get_length(bar_item, true);
    bool rtl = false;

    if (bar_item->position == POSITION_LEFT)
      next_position = &bar_left_first_item_x;
    else if (bar_item->position == POSITION_CENTER)
      next_position = &bar_center_first_item_x;
    else if (bar_item->position == POSITION_RIGHT)
      next_position = &bar_right_first_item_x, rtl = true;
    else if (bar_item->position == POSITION_CENTER_RIGHT)
      next_position = &bar_center_right_first_item_x;
    else if (bar_item->position == POSITION_CENTER_LEFT)
      next_position = &bar_center_left_first_item_x, rtl = true;
    else continue;

    if (bar_item->position == POSITION_RIGHT
        || bar_item->position == POSITION_CENTER_LEFT) {
      *next_position = min(*next_position - bar_item_display_length
                           - bar_item->background.padding_right,
                           bar->window.frame.size.width
                           - bar_item_display_length               );
    }
    else {
      *next_position += max((int)-*next_position,
                            bar_item->background.padding_left);
    }

    bar_item->graph.rtl = rtl;

    CGPoint shadow_offsets = bar_item_calculate_shadow_offsets(bar_item);
    uint32_t bar_item_length = bar_item_calculate_bounds(bar_item,
                                 g_bar_manager.background.bounds.size.height
                                 - (g_bar_manager.background.border_width + 1),
                                 max(shadow_offsets.x, 0),
                                 y                                           );

    CGRect frame = {{bar->window.origin.x + *next_position
                    - max(shadow_offsets.x, 0),
                     bar->window.origin.y                 },
                    {bar_item_display_length
                      + shadow_offsets.x
                      + shadow_offsets.y,
                     bar->window.frame.size.height}         };

    window_set_frame(bar_item_get_window(bar_item, bar->adid), frame);

    if (bar_item->popup.drawing)
      bar_calculate_popup_anchor_for_bar_item(bar, bar_item);

    if (bar_item->position == POSITION_RIGHT
        || bar_item->position == POSITION_CENTER_LEFT) {
      *next_position += bar_item->has_const_width
                        ? bar_item_display_length
                          + bar_item->background.padding_right
                          - bar_item->custom_width
                        : (- bar_item->background.padding_left);
    } else {
      *next_position += bar_item->has_const_width
                        ? bar_item->custom_width
                          - bar_item->background.padding_left
                        : (bar_item_length
                           + bar_item->background.padding_right);
    }
  }

  for (int i = 0; i < g_bar_manager.bar_item_count; i++) {
    struct bar_item* bar_item = g_bar_manager.bar_items[i];

    if (bar_item->type != BAR_COMPONENT_GROUP
        || bar_item->position == POSITION_POPUP
        || !bar_draws_item(bar, bar_item)) {
      continue;
    }

    group_calculate_bounds(bar_item->group, bar, y);
    window_set_frame(bar_item_get_window(bar_item->group->members[0],
                                         bar->adid                   ),
                     bar_item->group->bounds                           );

    if (bar_item->popup.drawing)
      bar_calculate_popup_anchor_for_bar_item(bar, bar_item);
  }
}

static void bar_calculate_bounds_left_right(struct bar* bar) {
  uint32_t notch_width = 0;

  uint32_t center_length = bar_manager_length_for_bar_side(&g_bar_manager,
                                                           bar,
                                                           POSITION_CENTER);

  uint32_t bar_left_first_item_y = max(g_bar_manager.background.padding_left,
                                       0                                     );

  uint32_t bar_right_first_item_y = bar->window.frame.size.height
                                   -max(g_bar_manager.background.padding_right,
                                          0                                  );

  uint32_t bar_center_first_item_y = (bar->window.frame.size.height
                                      - 2*g_bar_manager.margin
                                      - center_length) / 2 - 1;

  uint32_t bar_center_right_first_item_y = (bar->window.frame.size.height
                                            + notch_width) / 2;

  uint32_t bar_center_left_first_item_y = (bar->window.frame.size.height
                                           - notch_width) / 2 ;

  uint32_t* next_position = NULL;

  for (int i = 0; i < g_bar_manager.bar_item_count; i++) {
    struct bar_item* bar_item = g_bar_manager.bar_items[i];

    if (!bar_draws_item(bar, bar_item)
        || bar_item->type == BAR_COMPONENT_GROUP
        || bar_item->position == POSITION_POPUP ) {
      continue;
    } 

    uint32_t bar_item_display_height = bar_item_get_height(bar_item);
    uint32_t bar_item_display_length = bar_item_get_length(bar_item, true);
    uint32_t x =  0;
    bool rtl = false;

    if (bar_item->position == POSITION_LEFT)
      next_position = &bar_left_first_item_y;
    else if (bar_item->position == POSITION_CENTER)
      next_position = &bar_center_first_item_y;
    else if (bar_item->position == POSITION_RIGHT)
      next_position = &bar_right_first_item_y, rtl = true;
    else if (bar_item->position == POSITION_CENTER_RIGHT)
      next_position = &bar_center_right_first_item_y;
    else if (bar_item->position == POSITION_CENTER_LEFT)
      next_position = &bar_center_left_first_item_y, rtl = true;
    else continue;

    if (bar_item->position == POSITION_RIGHT
        || bar_item->position == POSITION_CENTER_LEFT) {

      *next_position = min(*next_position - bar_item_display_height
                           - bar_item->background.padding_right,
                           bar->window.frame.size.height
                           - bar_item_display_height               );
    }
    else {
      *next_position += max((int)-*next_position,
                            bar_item->background.padding_left);
    }

    bar_item->graph.rtl = rtl;

    CGPoint shadow_offsets = bar_item_calculate_shadow_offsets(bar_item);
    bar_item_calculate_bounds(bar_item,
                              bar_item_display_height,
                              (g_bar_manager.background.bounds.size.height
                               - bar_item_display_length) / 2.
                              + max(shadow_offsets.x, 0),
                              bar_item_display_height / 2.);


    CGRect frame = {{bar->window.origin.x + x
                     - max(shadow_offsets.x, 0),
                     bar->window.origin.y
                     + *next_position
                     + -max(-bar_item->y_offset, 0)},
                    {g_bar_manager.background.bounds.size.height,
                     bar_item_display_height + abs(bar_item->y_offset)}};

    window_set_frame(bar_item_get_window(bar_item, bar->adid), frame);

    if (bar_item->popup.drawing)
      bar_calculate_popup_anchor_for_bar_item(bar, bar_item);

    if (bar_item->position == POSITION_RIGHT
        || bar_item->position == POSITION_CENTER_LEFT) {
      *next_position += bar_item->has_const_width
                        ? bar_item_display_height
                          + bar_item->background.padding_right
                          - bar_item->custom_width
                        : - bar_item->background.padding_left;
    } else {
      *next_position += bar_item->has_const_width
                        ? bar_item->custom_width
                          - bar_item->background.padding_left
                        : (bar_item_display_height
                           + bar_item->background.padding_right);
    }
  }
}

void bar_calculate_bounds(struct bar* bar) {
  if (bar->sid < 1 || bar->adid < 1) return;

  if (g_bar_manager.position == POSITION_LEFT
      || g_bar_manager.position == POSITION_RIGHT) {
    bar_calculate_bounds_left_right(bar);
  } else {
    bar_calculate_bounds_top_bottom(bar);
  }
}

static CGRect bar_get_frame(struct bar *bar) {
  bool has_notch = workspace_display_notch_height(bar->did) > 0;
  int notch_offset = has_notch ? g_bar_manager.notch_offset : 0;


  CGRect bounds = display_bounds(bar->did);
  CGPoint origin = bounds.origin;

  if (g_bar_manager.position == POSITION_LEFT
      || g_bar_manager.position == POSITION_RIGHT) {
    bounds.size.height -= 2*g_bar_manager.background.y_offset;

    origin.x += (g_bar_manager.position == POSITION_RIGHT
                 ? (bounds.size.width
                    - g_bar_manager.background.bounds.size.height
                    - g_bar_manager.margin)
                 : g_bar_manager.margin);

    origin.y += g_bar_manager.background.y_offset;

    if (display_menu_bar_visible() && !g_bar_manager.topmost) {
      CGRect menu = display_menu_bar_rect(bar->did);
      origin.y += menu.size.height;
      bounds.size.height -= menu.size.height;
    }

    return (CGRect) {{origin.x, origin.y},
                     {g_bar_manager.background.bounds.size.height,
                      bounds.size.height                          }};
  } else {
    CGSize size = bar_get_shape_size_wide(bar,
                                          bar_uses_expanded_window(bar)
                                          ? BAR_EXPAND_PROGRESS_MAX
                                          : 0,
                                          bar_uses_wide_window(bar)
                                          ? BAR_EXPAND_PROGRESS_MAX
                                          : 0                          );

    // Centered on the display (keeps the display's own x origin, so this also
    // works on secondary monitors).
    origin.x = bounds.origin.x + round((bounds.size.width - size.width) / 2.0);
    origin.y = bounds.origin.y + g_bar_manager.background.y_offset
                               + notch_offset;

    if (g_bar_manager.position == POSITION_BOTTOM) {
      origin.y = CGRectGetMaxY(bounds)
                 - size.height
                 - 2*(g_bar_manager.background.y_offset) - notch_offset;
    } else if (display_menu_bar_visible() && !g_bar_manager.topmost) {
      CGRect menu = display_menu_bar_rect(bar->did);
      origin.y += menu.size.height;
    }

    return (CGRect) {origin, size};
  }
}

void bar_resize(struct bar* bar) {
  if (bar->hidden || !bar->shown) {
    window_move(&bar->window, g_nirvana);
    return;
  };
  window_set_frame(&bar->window, bar_get_frame(bar));
  if (window_apply_frame(&bar->window, false)) {
    window_assign_mouse_tracking_area(&bar->window, bar->window.frame);
    g_bar_manager.bar_needs_update = true;
  }
}

bool bar_set_expand_progress(void* target, int progress) {
  struct bar* bar = target;
  if (bar->expand_progress == progress) return false;
  bar->expand_progress = progress;
  // Only actually resizes the window once the collapse animation has
  // finished; every other frame just redraws the shape.
  bar_resize(bar);
  g_bar_manager.bar_needs_update = true;
  if (progress == 0 && !bar->expanded) bar_release_island_if_idle();
  return true;
}

// ---------------------------------------------------------- idle auto-close
// One timer for all bars, always set to the earliest idle deadline of any
// open island (parked far in the future while none is open).
static CFRunLoopTimerRef g_island_idle_timer = NULL;
static void bar_island_idle_handler(CFRunLoopTimerRef timer, void* context);

static void bar_island_schedule_idle() {
  CFAbsoluteTime next = 0;
  for (int i = 0; i < g_bar_manager.bar_count; i++) {
    struct bar* bar = g_bar_manager.bars[i];
    if (!bar || !bar->expanded) continue;
    CFAbsoluteTime deadline = bar->island_activity + BAR_ISLAND_IDLE_TIMEOUT;
    if (!next || deadline < next) next = deadline;
  }

  if (!g_island_idle_timer) {
    if (!next) return;
    g_island_idle_timer = CFRunLoopTimerCreate(NULL, next, 1e9, 0, 0,
                                               bar_island_idle_handler,
                                               NULL                    );
    CFRunLoopAddTimer(CFRunLoopGetMain(),
                      g_island_idle_timer,
                      kCFRunLoopCommonModes);
    return;
  }

  CFRunLoopTimerSetNextFireDate(g_island_idle_timer,
                                next ? next
                                     : CFAbsoluteTimeGetCurrent() + 1e9);
}

static void bar_island_idle_handler(CFRunLoopTimerRef timer, void* context) {
  CFAbsoluteTime now = CFAbsoluteTimeGetCurrent();
  for (int i = 0; i < g_bar_manager.bar_count; i++) {
    struct bar* bar = g_bar_manager.bars[i];
    if (!bar || !bar->expanded) continue;

    // Hovering counts as interaction for as long as it lasts.
    // So does an open Wi-Fi prompt (typing goes to the text field).
    if (bar->mouse_over || bar->island.prompt) bar->island_activity = now;
    else if (now - bar->island_activity >= BAR_ISLAND_IDLE_TIMEOUT - 0.05)
      bar_toggle_expanded(bar);
  }
  bar_island_schedule_idle();
  windows_unfreeze();
}

void bar_island_touch(struct bar* bar) {
  if (!bar->expanded) return;
  bar->island_activity = CFAbsoluteTimeGetCurrent();
  bar_island_schedule_idle();
}

static bool bar_any_expanded() {
  for (int i = 0; i < g_bar_manager.bar_count; i++) {
    if (g_bar_manager.bars[i] && g_bar_manager.bars[i]->expanded) return true;
  }
  return false;
}

// ------------------------------------------------------------ system stats
static CFRunLoopTimerRef g_stats_timer = NULL;

static void bar_island_stats_handler(CFRunLoopTimerRef timer, void* context) {
  system_stats_sample_load(&g_island_stats);
  system_stats_history_push(&g_island_history, &g_island_stats);
  if (!bar_any_expanded()) return;

  // Redraw even when the values didn't change: the graphs move on.
  system_stats_sample_battery(&g_island_stats);

  // Brightness keys may have been used: follow them (System tab only, not
  // while the slider is being dragged).
  for (int i = 0; i < g_bar_manager.bar_count; i++) {
    struct bar* bar = g_bar_manager.bars[i];
    if (bar && bar->expanded && bar->island.service == ISLAND_SERVICE_SYSTEM
        && !bar->island.dragging_brightness                                ) {
      bar_island_update_brightness(bar);
    }
  }
  g_bar_manager.bar_needs_update = true;
  bar_manager_refresh(&g_bar_manager, false);
  windows_unfreeze();
}

// Now playing / audio output changed (main thread).
static void bar_island_sources_changed(void) {
  if (!bar_any_expanded()) return;
  g_bar_manager.bar_needs_update = true;
  bar_manager_refresh(&g_bar_manager, false);
  windows_unfreeze();
}

// Started with the first bar and never stopped, see BAR_STATS_INTERVAL.
static void bar_start_stats_history() {
  if (g_stats_timer) return;

  // Takes the CPU baseline for the first tick.
  system_stats_sample_load(&g_island_stats);
  g_stats_timer = CFRunLoopTimerCreate(NULL,
                                       CFAbsoluteTimeGetCurrent()
                                       + BAR_STATS_INTERVAL,
                                       BAR_STATS_INTERVAL,
                                       0,
                                       0,
                                       bar_island_stats_handler,
                                       NULL                      );
  CFRunLoopTimerSetTolerance(g_stats_timer, BAR_STATS_TOLERANCE);
  CFRunLoopAddTimer(CFRunLoopGetMain(), g_stats_timer, kCFRunLoopCommonModes);
}

// ------------------------------------------------------------ wifi prompt
// The bar whose Wi-Fi prompt owns the password field.
static struct bar* g_prompt_bar = NULL;

static bool bar_is_alive(struct bar* bar) {
  for (int i = 0; i < g_bar_manager.bar_count; i++) {
    if (g_bar_manager.bars[i] == bar) return true;
  }
  return false;
}

static void bar_island_redraw() {
  g_bar_manager.bar_needs_update = true;
  bar_manager_refresh(&g_bar_manager, false);
  windows_unfreeze();
}

static void bar_island_join(struct bar* bar, const char* password) {
  bar->island.prompt_joining = true;
  password_field_hide();
  connections_join_wifi(bar->island.prompt_ssid,
                        password && password[0] ? password : NULL);
  g_bar_manager.bar_needs_update = true;
}

// Return (submitted) or Escape in the password field.
static void bar_island_password_done(bool submitted, const char* password) {
  struct bar* bar = g_prompt_bar;
  if (!bar || !bar_is_alive(bar) || !bar->island.prompt) return;

  if (submitted) bar_island_join(bar, password);
  else bar_island_close_prompt(bar);
  bar_island_redraw();
}

// Lays the real text field over the drawn one, pre-filled with the password
// that was tried last for this network.
static void bar_island_show_password_field(struct bar* bar) {
  if (!bar->island.prompt || bar->island.prompt_joining) return;

  CGRect local = island_prompt_field_rect(&bar->island,
                                          bar_get_island_panel(bar));
  CGRect global = {{ bar->window.origin.x + local.origin.x,
                     bar->window.origin.y + bar->window.frame.size.height
                     - CGRectGetMaxY(local)                             },
                   local.size                                             };
  g_prompt_bar = bar;
  password_field_show(global,
                      connections_wifi_password(bar->island.prompt_ssid),
                      bar_island_password_done                          );
}

static bool bar_set_wide_progress(void* target, int value) {
  struct bar* bar = target;
  if (bar->wide_progress == value) return false;
  bar->wide_progress = value;
  bar->island.prompt_progress = bar_progress_fraction(value);

  if (value == 0 && !bar->wide) {
    bar->island.prompt = false;
    bar->island.prompt_joining = false;
  }

  // Shrinks the window back once the island is narrow again.
  bar_resize(bar);
  g_bar_manager.bar_needs_update = true;

  // The field can only be placed once the island stopped moving.
  if (value == BAR_EXPAND_PROGRESS_MAX && bar->wide)
    bar_island_show_password_field(bar);
  return true;
}

static void bar_cancel_wide(struct bar* bar) {
  animator_cancel_locked(&g_bar_manager.animator,
                         bar,
                         (animator_function*)bar_set_wide_progress);
}

static void bar_animate_wide(struct bar* bar) {
  bar_cancel_wide(bar);

  bool needs_refresh = false;
  uint32_t saved_duration = g_bar_manager.animator.duration;
  char saved_interp = g_bar_manager.animator.interp_function;
  g_bar_manager.animator.duration = BAR_WIDE_DURATION;
  g_bar_manager.animator.interp_function = INTERP_FUNCTION_SIN;

  ANIMATE(bar_set_wide_progress,
          bar,
          bar->wide_progress,
          bar->wide ? BAR_EXPAND_PROGRESS_MAX : 0);
  animator_lock(&g_bar_manager.animator);

  g_bar_manager.animator.duration = saved_duration;
  g_bar_manager.animator.interp_function = saved_interp;
  (void)needs_refresh;
}

// Widens the island, fades the Bluetooth column out and the prompt in. With
// a prompt already open it just switches to the other network.
static void bar_island_open_prompt(struct bar* bar, const char* ssid) {
  struct island* island = &bar->island;
  snprintf(island->prompt_ssid, sizeof(island->prompt_ssid), "%s", ssid);
  island->prompt = true;
  island->prompt_joining = false;

  if (!bar->wide) {
    bar->wide = true;
    // Grow the window first, so the animation never resizes it.
    bar_resize(bar);
    bar_animate_wide(bar);
  } else if (bar->wide_progress == BAR_EXPAND_PROGRESS_MAX) {
    bar_island_show_password_field(bar);
  }
  g_bar_manager.bar_needs_update = true;
}

static void bar_island_close_prompt(struct bar* bar) {
  if (g_prompt_bar == bar) {
    password_field_hide();
    g_prompt_bar = NULL;
  }
  if (!bar->wide) return;
  bar->wide = false;
  bar_animate_wide(bar);
  g_bar_manager.bar_needs_update = true;
}

// ------------------------------------------------------------ switches
// A clicked switch flips right away (optimistic) and keeps that until the
// real state catches up, or BAR_TOGGLE_PENDING seconds passed.
static int g_toggle_pending[2] = { -1, -1 };
static CFAbsoluteTime g_toggle_pending_since[2];

static bool bar_toggle_actual(int column) {
  const struct connections* connections = connections_get();
  return column == 0 ? connections->wifi_on : connections->bluetooth_on;
}

static bool bar_toggle_desired(int column) {
  bool actual = bar_toggle_actual(column);
  if (g_toggle_pending[column] < 0) return actual;

  if (g_toggle_pending[column] == actual
      || CFAbsoluteTimeGetCurrent() - g_toggle_pending_since[column]
         > BAR_TOGGLE_PENDING                                       ) {
    g_toggle_pending[column] = -1;
    return actual;
  }
  return g_toggle_pending[column];
}

static bool bar_set_toggle(struct bar* bar, int column, int value) {
  double progress = bar_progress_fraction(value);
  if (bar->island.toggle_progress[column] == progress) return false;
  bar->island.toggle_progress[column] = progress;
  g_bar_manager.bar_needs_update = true;
  return true;
}

static bool bar_set_wifi_toggle(void* target, int value) {
  return bar_set_toggle(target, 0, value);
}

static bool bar_set_bluetooth_toggle(void* target, int value) {
  return bar_set_toggle(target, 1, value);
}

static animator_function* const g_toggle_setters[2] = {
  bar_set_wifi_toggle, bar_set_bluetooth_toggle
};

static void bar_cancel_toggles(struct bar* bar) {
  for (int i = 0; i < 2; i++) {
    animator_cancel_locked(&g_bar_manager.animator, bar, g_toggle_setters[i]);
  }
}

// Moves both switches of `bar` to their desired state: animated, or right
// away for the first sync after opening the tab.
static void bar_sync_toggles(struct bar* bar) {
  struct island* island = &bar->island;
  for (int column = 0; column < 2; column++) {
    double target = bar_toggle_desired(column) ? 1.0 : 0.0;

    if (!island->toggles_ready) {
      island->toggle_progress[column] = target;
      continue;
    }
    if (island->toggle_progress[column] == target) continue;

    animator_cancel_locked(&g_bar_manager.animator, bar,
                           g_toggle_setters[column]     );
    bool needs_refresh = false;
    uint32_t saved_duration = g_bar_manager.animator.duration;
    char saved_interp = g_bar_manager.animator.interp_function;
    g_bar_manager.animator.duration = BAR_TOGGLE_DURATION;
    g_bar_manager.animator.interp_function = INTERP_FUNCTION_SIN;

    // ANIMATE takes the function's address (&f), so it needs the function
    // itself, not a function pointer variable.
    int from = (int)round(island->toggle_progress[column]
                          * BAR_EXPAND_PROGRESS_MAX     );
    int to = (int)(target * BAR_EXPAND_PROGRESS_MAX);
    if (column == 0) ANIMATE(bar_set_wifi_toggle, bar, from, to)
    else ANIMATE(bar_set_bluetooth_toggle, bar, from, to)
    animator_lock(&g_bar_manager.animator);

    g_bar_manager.animator.duration = saved_duration;
    g_bar_manager.animator.interp_function = saved_interp;
    (void)needs_refresh;
  }
  island->toggles_ready = true;
  g_bar_manager.bar_needs_update = true;
}

static void bar_click_toggle(struct bar* bar, int column) {
  bool on = !bar_toggle_desired(column);
  g_toggle_pending[column] = on;
  g_toggle_pending_since[column] = CFAbsoluteTimeGetCurrent();

  if (column == 0) connections_set_wifi_power(on);
  else connections_set_bluetooth_power(on);
  bar_sync_toggles(bar);

  // If the change never happens, nothing else redraws: look again once the
  // optimistic state expired, so the switch slides back.
  dispatch_after(dispatch_time(DISPATCH_TIME_NOW,
                               (BAR_TOGGLE_PENDING + 0.1) * NSEC_PER_SEC),
                 dispatch_get_main_queue(),
                 ^{ bar_island_connections_changed(); });
}

// A submitted prompt closes once its result was shown (the notice clears).
static void bar_island_connections_changed(void) {
  const struct connections* connections = connections_get();
  for (int i = 0; i < g_bar_manager.bar_count; i++) {
    struct bar* bar = g_bar_manager.bars[i];
    if (bar && bar->expanded
        && bar->island.service == ISLAND_SERVICE_CONNECTIONS) {
      bar_sync_toggles(bar);
    }
  }

  for (int i = 0; i < g_bar_manager.bar_count; i++) {
    struct bar* bar = g_bar_manager.bars[i];
    if (bar && bar->island.prompt && bar->island.prompt_joining
        && connections->status == CONNECTION_IDLE                ) {
      bar_island_close_prompt(bar);
    }
  }
  bar_island_sources_changed();
}

// Runs the Music/System tabs' sources (player, audio output, network)
// exactly while some island is open.
static void bar_island_update_sources() {
  bool needed = bar_any_expanded();

  if (needed) {
    now_playing_start(bar_island_sources_changed);
    audio_output_start(bar_island_sources_changed);
    system_info_start(bar_island_sources_changed);
  } else {
    now_playing_stop();
    audio_output_stop();
    system_info_stop();
  }

  // Wi-Fi/Bluetooth only while their tab is showing.
  bool connections = false;
  for (int i = 0; i < g_bar_manager.bar_count; i++) {
    struct bar* bar = g_bar_manager.bars[i];
    if (bar && bar->expanded
        && bar->island.service == ISLAND_SERVICE_CONNECTIONS) {
      connections = true;
    }
  }
  if (connections) connections_start(bar_island_connections_changed);
  else connections_stop();

  // CPU/RAM are already current (history timer); the battery isn't read
  // while closed, so read it now.
  if (needed) system_stats_sample_battery(&g_island_stats);
}

void bar_toggle_expanded(struct bar* bar) {
  bar->expanded = !bar->expanded;
  bar_cancel_island_scroll(bar);
  bar_cancel_island_transition(bar);
  if (!bar->expanded) bar_island_close_prompt(bar);
  if (bar->expanded) {
    island_open(&bar->island,
                g_bar_manager.default_item.label.font.family);
    bar->island_activity = CFAbsoluteTimeGetCurrent();
    if (bar->island.service == ISLAND_SERVICE_SCREENS)
      bar_island_update_desktops(bar);
    if (bar->island.service == ISLAND_SERVICE_SYSTEM)
      bar_island_update_brightness(bar);
  }
  bar_island_schedule_idle();

  // Clicks outside of our windows are only observed while an island is open.
  mouse_watch_outside_clicks(bar_any_expanded());
  bar_island_update_sources();

  // Grow the window to its expanded size before the first animation frame so
  // that the animation never has to resize it.
  bar_resize(bar);
  g_bar_manager.bar_needs_update = true;

  bool needs_refresh = false;
  uint32_t duration = g_bar_manager.animator.duration;
  char interp_function = g_bar_manager.animator.interp_function;
  g_bar_manager.animator.duration = BAR_EXPAND_DURATION;
  g_bar_manager.animator.interp_function = INTERP_FUNCTION_SIN;

  ANIMATE(bar_set_expand_progress,
          bar,
          bar->expand_progress,
          bar->expanded ? BAR_EXPAND_PROGRESS_MAX : 0);

  // Lock it so that a click during the animation cancels it and reverses
  // from the current progress instead of being queued behind it.
  animator_lock(&g_bar_manager.animator);
  g_bar_manager.animator.duration = duration;
  g_bar_manager.animator.interp_function = interp_function;

  (void)needs_refresh;
  bar_manager_refresh(&g_bar_manager, false);
}

void bar_set_hidden(struct bar* bar, bool hidden) {
  if (bar->hidden == hidden) return;
  bar->hidden = hidden;
  
  if (hidden) window_move(&bar->window, g_nirvana);
  else bar_resize(bar);
}

static void bar_create_window(struct bar* bar) {
  window_init(&bar->window);
  window_open(&bar->window, bar_get_frame(bar));
  window_assign_mouse_tracking_area(&bar->window, bar->window.frame);
  window_set_blur_radius(&bar->window, g_bar_manager.blur_radius);
  if (!g_bar_manager.shadow) window_disable_shadow(&bar->window);
}

void bar_change_space(struct bar* bar, uint64_t dsid) {
  if (bar->adid < 1) return;

  for (uint32_t i = 0; i < g_bar_manager.bar_item_count; i++) {
    struct bar_item* bar_item = g_bar_manager.bar_items[i];
    bar_item_change_space(bar_item, dsid, bar->adid);
  }

  window_send_to_space(&bar->window, dsid);
}

struct bar *bar_create(uint32_t did) {
  struct bar *bar = malloc(sizeof(struct bar));
  memset(bar, 0, sizeof(struct bar));
  bar->hidden = false;
  bar->mouse_over = false;
  bar->did = did;
  bar->notch_width = workspace_display_notch_width(did);
  bar->dsid = display_space_id(did);
  bar->sid = mission_control_index(bar->dsid);
  bar->shown = SLSSpaceGetType(g_connection, bar->dsid) != 4;
  g_bar_manager.bar_needs_update = true;
  bar_create_window(bar);
  bar_start_stats_history();
  return bar;
}

void bar_destroy(struct bar *bar) {
  // Don't leave a running expand animation pointing at freed memory.
  animator_cancel_locked(&g_bar_manager.animator,
                         bar,
                         (animator_function*)bar_set_expand_progress);
  bar_cancel_island_scroll(bar);
  bar_cancel_island_transition(bar);
  bar_cancel_wide(bar);
  bar_cancel_toggles(bar);
  if (g_prompt_bar == bar) {
    password_field_hide();
    g_prompt_bar = NULL;
  }
  window_close(&bar->window);
  free(bar);
}
