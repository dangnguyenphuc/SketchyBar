#include "bar.h"
#include "bar_manager.h"
#include "event.h"
#include "display.h"
#include "misc/helpers.h"
#include "window.h"
#include "mouse.h"
#include "stats.h"

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

// Size of the visible notch shape for a given expand progress.
static CGSize bar_get_shape_size(struct bar* bar, int progress) {
  double t = bar_progress_fraction(progress);
  double collapsed_height = bar_collapsed_height(bar);
  return (CGSize){
    round(BAR_NOTCH_COLLAPSED_WIDTH
          + (BAR_NOTCH_EXPANDED_WIDTH - BAR_NOTCH_COLLAPSED_WIDTH) * t),
    round(collapsed_height
          + (BAR_NOTCH_EXPANDED_HEIGHT - collapsed_height) * t)
  };
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
static bool bar_uses_peek_window(struct bar* bar) {
  return bar_is_horizontal()
         && !bar_uses_expanded_window(bar)
         && (bar->peeking || bar->peek_progress > 0);
}

static CGRect bar_get_shape_bounds(struct bar* bar) {
  CGRect frame = bar->window.frame;
  if (!bar_is_horizontal()) return frame;

  // Peek: the collapsed shape grows into the wider window, `peek_left` to
  // the left and `peek_right` to the right of where it normally sits.
  if (bar_uses_peek_window(bar)) {
    CGSize collapsed = bar_get_shape_size(bar, 0);
    double t = bar_progress_fraction(bar->peek_progress);
    frame.origin.x = round(bar->peek_left * (1.0 - t));
    frame.origin.y = g_bar_manager.position == POSITION_BOTTOM
                     ? 0
                     : frame.size.height - collapsed.height;
    frame.size = (CGSize){ round(collapsed.width
                                 + (bar->peek_left + bar->peek_right) * t),
                           collapsed.height                              };
    return frame;
  }

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

// The fully expanded shape (window-local, y-up): what the content is laid
// out in. It stays put during the expand animation (the shape only clips
// it).
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

static void bar_island_update_sources();
static void bar_island_update_desktops(struct bar* bar);
static void bar_island_update_brightness(struct bar* bar);
static void bar_click_toggle(struct bar* bar, int column);
static void bar_sync_toggles(struct bar* bar);
static void bar_island_connections_changed(void);
static void bar_animate_app_toggle(struct bar* bar);
static void bar_animate_claude_transition(struct bar* bar);

// Opens a System Settings page (an x-apple.systempreferences: URL).
static void bar_open_url(const char* string) {
  CFURLRef url = CFURLCreateWithBytes(NULL, (const UInt8*)string,
                                      strlen(string),
                                      kCFStringEncodingUTF8, NULL);
  if (!url) return;
  LSOpenCFURLRef(url, NULL);
  CFRelease(url);
}

// Runs what an input event on the island content asked for.
static void bar_island_perform(struct bar* bar, struct island_action* action) {
  const struct connections* connections = connections_get();
  switch (action->type) {
    case ISLAND_ACTION_WIFI_POWER:
      bar_click_toggle(bar, ISLAND_TOGGLE_WIFI);
      break;
    case ISLAND_ACTION_WIFI_SETTINGS:
      bar_open_url(BAR_WIFI_SETTINGS_URL);
      break;
    case ISLAND_ACTION_BLUETOOTH_POWER:
      bar_click_toggle(bar, ISLAND_TOGGLE_BLUETOOTH);
      break;
    case ISLAND_ACTION_BLUETOOTH_SETTINGS:
      bar_open_url(BAR_BLUETOOTH_SETTINGS_URL);
      break;
    case ISLAND_ACTION_BLUETOOTH_SELECT:
      if (action->index < 0 || action->index >= connections->bluetooth_count
          || connections->status == CONNECTION_CONNECTING                   ) {
        break;
      }
      connections_toggle_bluetooth(connections->bluetooth[action->index]
                                                .address              );
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
    case ISLAND_ACTION_NOTIFICATION_OPEN: {
      // Claude's notifications open their project in VS Code (`code`; if
      // it isn't set up, the notification is only marked read).
      const struct notification_list* list = notifications_get();
      if (action->index >= 0 && action->index < list->count
          && list->items[action->index].claude              ) {
        claude_open_path(list->items[action->index].path);
        notifications_dismiss(action->index);
      } else {
        notifications_open(action->index);
      }
      island_clamp_scroll(&bar->island, bar_get_island_panel(bar));
      break;
    }
    case ISLAND_ACTION_SUGGESTION_YES:
    case ISLAND_ACTION_SUGGESTION_NO: {
      const struct notification_list* list = notifications_get();
      if (action->index < 0 || action->index >= list->count) break;
      struct notification_item item = list->items[action->index];
      claude_answer(&item, action->type == ISLAND_ACTION_SUGGESTION_YES);
      island_clamp_scroll(&bar->island, bar_get_island_panel(bar));
      break;
    }
    case ISLAND_ACTION_CLAUDE_SUBTAB:
      bar_animate_claude_transition(bar);
      break;
    case ISLAND_ACTION_CLAUDE_OPEN: {
      const struct claude_state* claude = claude_get();
      if (action->index >= 0 && action->index < claude->session_count)
        claude_open_path(claude->sessions[action->index].cwd);
      break;
    }
    case ISLAND_ACTION_CLAUDE_STOP:
      claude_stop_session(action->index);
      break;
    case ISLAND_ACTION_CLAUDE_RETRY:
      claude_check_now();
      break;
    case ISLAND_ACTION_CLAUDE_REFRESH:
      claude_refresh_models();
      break;
    case ISLAND_ACTION_NOTIFICATION_DISMISS:
      notifications_dismiss(action->index);
      island_clamp_scroll(&bar->island, bar_get_island_panel(bar));
      break;
    case ISLAND_ACTION_NOTIFICATIONS_READ_ALL:
      notifications_read_all();
      island_clamp_scroll(&bar->island, bar_get_island_panel(bar));
      break;
    case ISLAND_ACTION_APP_TOGGLE: {
      const struct notification_apps* apps = notifications_apps();
      if (action->index < 0 || action->index >= apps->count) break;
      // Starts the slide before the change redraws the island.
      snprintf(bar->island.app_toggle, sizeof(bar->island.app_toggle), "%s",
               apps->items[action->index].name                            );
      bar->island.app_toggle_progress = 0;
      notifications_set_app_enabled(action->index,
                                    !apps->items[action->index].enabled);
      bar_animate_app_toggle(bar);
      break;
    }
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

// One frame of the Alerts cards' animations (see island_step_cards). The
// driver's value only reaches 1 on its last frame: then everything settles.
static bool bar_step_island_cards(void* target, int value) {
  struct bar* bar = target;
  uint64_t now = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
  double dt = (double)(now - bar->cards_time) / 1e9;
  if (!bar->cards_time || dt <= 0 || dt > 0.05) dt = 1.0 / 60.0;
  bar->cards_time = now;

  island_step_cards(&bar->island, dt, value >= 1);
  g_bar_manager.bar_needs_update = true;
  return true;
}

static void bar_cancel_island_cards(struct bar* bar) {
  animator_cancel_locked(&g_bar_manager.animator,
                         bar,
                         (animator_function*)bar_step_island_cards);
}

static void bar_animate_island_cards(struct bar* bar) {
  bar_cancel_island_cards(bar);
  bar->cards_time = 0;

  bool needs_refresh = false;
  uint32_t saved_duration = g_bar_manager.animator.duration;
  char saved_interp = g_bar_manager.animator.interp_function;
  g_bar_manager.animator.duration = BAR_CARDS_FRAMES;
  g_bar_manager.animator.interp_function = INTERP_FUNCTION_LINEAR;

  ANIMATE(bar_step_island_cards, bar, 0, 1);
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

// Apps tab: the clicked app's switch slides (island.app_toggle_progress).
static bool bar_set_app_toggle(void* target, int value) {
  struct bar* bar = target;
  double progress = (double)value / BAR_EXPAND_PROGRESS_MAX;
  if (bar->island.app_toggle_progress == progress) return false;
  bar->island.app_toggle_progress = progress;
  return true;
}

static void bar_animate_app_toggle(struct bar* bar) {
  animator_cancel_locked(&g_bar_manager.animator,
                         bar,
                         (animator_function*)bar_set_app_toggle);

  bool needs_refresh = false;
  uint32_t saved_duration = g_bar_manager.animator.duration;
  char saved_interp = g_bar_manager.animator.interp_function;
  g_bar_manager.animator.duration = BAR_TOGGLE_DURATION;
  g_bar_manager.animator.interp_function = INTERP_FUNCTION_SIN;

  ANIMATE(bar_set_app_toggle, bar, 0, BAR_EXPAND_PROGRESS_MAX);
  animator_lock(&g_bar_manager.animator);

  g_bar_manager.animator.duration = saved_duration;
  g_bar_manager.animator.interp_function = saved_interp;
  (void)needs_refresh;
}

// Claude tab: the subtab pages slide (island.claude_transition).
static bool bar_set_claude_transition(void* target, int value) {
  struct bar* bar = target;
  double transition = (double)value / BAR_EXPAND_PROGRESS_MAX;
  if (bar->island.claude_transition == transition) return false;
  bar->island.claude_transition = transition;
  return true;
}

static void bar_animate_claude_transition(struct bar* bar) {
  animator_cancel_locked(&g_bar_manager.animator,
                         bar,
                         (animator_function*)bar_set_claude_transition);

  bool needs_refresh = false;
  uint32_t saved_duration = g_bar_manager.animator.duration;
  char saved_interp = g_bar_manager.animator.interp_function;
  g_bar_manager.animator.duration = BAR_PAGE_DURATION;
  g_bar_manager.animator.interp_function = INTERP_FUNCTION_SIN;

  ANIMATE(bar_set_claude_transition, bar, 0, BAR_EXPAND_PROGRESS_MAX);
  animator_lock(&g_bar_manager.animator);

  g_bar_manager.animator.duration = saved_duration;
  g_bar_manager.animator.interp_function = saved_interp;
  (void)needs_refresh;
}

// ------------------------------------------------------------ sprites
// While the island draws a moving sprite (Clawd), everything is redrawn
// ISLAND_SPRITE_FPS times a second; the timer stops on the first redraw
// that drew none.
static CFRunLoopTimerRef g_sprite_timer = NULL;

static void bar_sprite_handler(CFRunLoopTimerRef timer, void* context) {
  island_animation_reset();
  g_bar_manager.bar_needs_update = true;
  bar_manager_refresh(&g_bar_manager, false);
  windows_unfreeze();
  if (!island_animating()) {
    CFRunLoopTimerInvalidate(g_sprite_timer);
    CFRelease(g_sprite_timer);
    g_sprite_timer = NULL;
  }
}

static void bar_start_sprite_timer() {
  if (g_sprite_timer) return;
  double interval = 1.0 / ISLAND_SPRITE_FPS;
  g_sprite_timer = CFRunLoopTimerCreate(NULL,
                                        CFAbsoluteTimeGetCurrent() + interval,
                                        interval, 0, 0,
                                        bar_sprite_handler, NULL             );
  CFRunLoopAddTimer(CFRunLoopGetMain(), g_sprite_timer, kCFRunLoopCommonModes);
}

bool bar_contains_point(struct bar* bar, CGPoint point) {
  CGRect frame = bar->window.frame;
  frame.origin = bar->window.origin;
  return CGRectContainsPoint(frame, point);
}

static bool bar_peek_click(struct bar* bar, CGPoint point);

bool bar_island_click(struct bar* bar, CGPoint point) {
  if (bar_peek_click(bar, point)) return true;
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
    bar_animate_island_transition(bar);
    bar_island_update_sources();
    if (bar->island.service == ISLAND_SERVICE_BLUETOOTH
        || bar->island.service == ISLAND_SERVICE_SYSTEM) {
      bar_sync_toggles(bar);
    }
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
    if (bar && (bar->expanded || bar->expand_progress > 0
                || bar->peeking || bar->peek_progress > 0)) {
      return;
    }
  }
  island_release();
}

// ------------------------------------------------------------ peek layout

static double bar_notch_width(struct bar* bar) {
  return bar->notch_width > 0 ? bar->notch_width : g_bar_manager.notch_width;
}

// Where the peek's icon (left of the camera) and text (right of it) go,
// window-local (y-up), vertically centered in the collapsed band.
static void bar_get_peek_rects(struct bar* bar, CGRect* icon, CGRect* text) {
  CGSize collapsed = bar_get_shape_size(bar, 0);
  CGFloat height = collapsed.height;
  CGFloat bottom = g_bar_manager.position == POSITION_BOTTOM
                   ? 0
                   : bar->window.frame.size.height - height;
  // The window starts `peek_left` left of the collapsed shape.
  CGFloat center = bar->peek_left + collapsed.width / 2.0;
  CGFloat notch = bar_notch_width(bar);

  // A suggestion's Yes / No take the icon's place, as tall as the band.
  CGFloat icon_width = bar->peek_suggestion ? ISLAND_CHOICES_WIDTH
                                            : BAR_PEEK_ICON;
  CGFloat icon_height = bar->peek_suggestion ? height : BAR_PEEK_ICON;
  *icon = (CGRect){{ center - notch / 2.0 - BAR_PEEK_PADDING - icon_width,
                     bottom + (height - icon_height) / 2.0              },
                   { icon_width, icon_height }};
  CGFloat text_x = center + notch / 2.0 + BAR_PEEK_PADDING;
  *text = (CGRect){{ text_x, bottom },
                   { bar->window.frame.size.width - text_x
                     - BAR_PEEK_PADDING
                     - g_bar_manager.background.corner_radius / 2.0,
                     height                                         }};
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

    if (bar_uses_peek_window(bar) && bar->peek_progress > 0) {
      CGRect icon, text;
      bar_get_peek_rects(bar, &icon, &text);
      // The icon and text fade in over the last part of the growth.
      double t = bar_progress_fraction(bar->peek_progress);
      if (bar->peek_suggestion) {
        island_draw_suggestion_peek(bar->window.surface->context,
                                    &bar->peek_item, icon, text,
                                    (t - 0.6) / 0.4              );
      } else {
        island_draw_peek(bar->window.surface->context, &bar->peek_item,
                         bar->peek_text, icon, text, (t - 0.6) / 0.4    );
      }
    }

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
    if (island_animating()) bar_start_sprite_timer();
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
    CGSize size = bar_get_shape_size(bar, bar_uses_expanded_window(bar)
                                          ? BAR_EXPAND_PROGRESS_MAX
                                          : 0                       );

    // Centered on the display (keeps the display's own x origin, so this also
    // works on secondary monitors). A peek widens it unevenly: the collapsed
    // shape stays centered, the window reaches further out on each side.
    origin.x = bounds.origin.x + round((bounds.size.width - size.width) / 2.0);
    if (bar_uses_peek_window(bar)) {
      origin.x -= bar->peek_left;
      size.width += bar->peek_left + bar->peek_right;
    }
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
  double timeout = g_bar_manager.island_idle_timeout;
  for (int i = 0; i < g_bar_manager.bar_count && timeout > 0; i++) {
    struct bar* bar = g_bar_manager.bars[i];
    if (!bar || !bar->expanded) continue;
    CFAbsoluteTime deadline = bar->island_activity + timeout;
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
  double timeout = g_bar_manager.island_idle_timeout;
  for (int i = 0; i < g_bar_manager.bar_count && timeout > 0; i++) {
    struct bar* bar = g_bar_manager.bars[i];
    if (!bar || !bar->expanded) continue;

    // Hovering counts as interaction for as long as it lasts.
    if (bar->mouse_over) bar->island_activity = now;
    else if (now - bar->island_activity >= timeout - 0.05)
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

void bar_island_settings_changed(void) {
  bar_island_schedule_idle();
  // A plugin may have been switched: its sources start or stop.
  bar_island_update_sources();
  for (int i = 0; i < g_bar_manager.bar_count; i++) {
    struct bar* bar = g_bar_manager.bars[i];
    if (bar && bar->expanded)
      island_clamp_scroll(&bar->island, bar_get_island_panel(bar));
  }
  g_bar_manager.bar_needs_update = true;
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

// A source of the island changed (main thread): keep the switches in step
// with the real Wi-Fi/Bluetooth state, then redraw.
static void bar_island_sources_changed(void) {
  if (!bar_any_expanded()) return;
  for (int i = 0; i < g_bar_manager.bar_count; i++) {
    struct bar* bar = g_bar_manager.bars[i];
    if (bar && bar->expanded
        && (bar->island.service == ISLAND_SERVICE_SYSTEM
            || bar->island.service == ISLAND_SERVICE_BLUETOOTH)) {
      bar_sync_toggles(bar);
    }
  }
  g_bar_manager.bar_needs_update = true;
  bar_manager_refresh(&g_bar_manager, false);
  windows_unfreeze();
}

// ------------------------------------------------------------ peek

static bool bar_is_alive(struct bar* bar) {
  for (int i = 0; i < g_bar_manager.bar_count; i++) {
    if (g_bar_manager.bars[i] == bar) return true;
  }
  return false;
}

static bool bar_set_peek_progress(void* target, int value) {
  struct bar* bar = target;
  if (bar->peek_progress == value) return false;
  bar->peek_progress = value;
  g_bar_manager.bar_needs_update = true;

  // Shrunk back: the window returns to the collapsed size.
  if (value == 0 && !bar->peeking) {
    bar->peek_left = bar->peek_right = 0;
    bar_resize(bar);
    bar_release_island_if_idle();
  }
  return true;
}

static void bar_cancel_peek(struct bar* bar) {
  animator_cancel_locked(&g_bar_manager.animator,
                         bar,
                         (animator_function*)bar_set_peek_progress);
}

static void bar_animate_peek(struct bar* bar) {
  bar_cancel_peek(bar);

  bool needs_refresh = false;
  uint32_t saved_duration = g_bar_manager.animator.duration;
  char saved_interp = g_bar_manager.animator.interp_function;
  g_bar_manager.animator.duration = BAR_PEEK_DURATION;
  g_bar_manager.animator.interp_function = INTERP_FUNCTION_SIN;

  ANIMATE(bar_set_peek_progress,
          bar,
          bar->peek_progress,
          bar->peeking ? BAR_EXPAND_PROGRESS_MAX : 0);
  animator_lock(&g_bar_manager.animator);

  g_bar_manager.animator.duration = saved_duration;
  g_bar_manager.animator.interp_function = saved_interp;
  (void)needs_refresh;
}

// Ends a peek right away (the island is being opened).
static void bar_stop_peek(struct bar* bar) {
  if (!bar->peeking && bar->peek_progress == 0) return;
  bar_cancel_peek(bar);
  bar->peeking = false;
  bar->has_peek_next = false;
  bar->peek_token++;
  bar->peek_progress = 0;
  bar->peek_left = bar->peek_right = 0;
}

// Shows `item` in the closed island: grow (BAR_PEEK_DURATION), hold
// (notification_peek_duration), shrink (BAR_PEEK_DURATION). A newer one
// during a peek replaces it and starts the hold again.
static void bar_peek(struct bar* bar, const struct notification_item* item) {
  island_open(&bar->island, g_bar_manager.default_item.label.font.family);
  bar->peek_item = *item;
  bar->peek_suggestion = claude_suggestion_active(item);
  CGFloat text = bar->peek_suggestion
                 ? island_suggestion_text_width(item->suggestion)
                 : island_peek_text(item,
                                    g_bar_manager.notification_peek_chars,
                                    bar->peek_text, sizeof(bar->peek_text));

  // How far past the collapsed shape each side has to reach: the icon (or
  // Yes / No) left of the camera, the text right of it (clear of the
  // rounded corner).
  CGFloat collapsed = bar_get_shape_size(bar, 0).width;
  CGFloat beside = (collapsed - bar_notch_width(bar)) / 2.0;
  CGFloat corner = g_bar_manager.background.corner_radius / 2.0;
  CGFloat left = bar->peek_suggestion ? ISLAND_CHOICES_WIDTH : BAR_PEEK_ICON;
  bar->peek_left = max(ceil(BAR_PEEK_PADDING * 2 + left + corner - beside),
                       0                                                   );
  bar->peek_right = max(ceil(BAR_PEEK_PADDING * 2 + text + corner - beside),
                        0                                                  );

  bar->peeking = true;
  bar_resize(bar);
  g_bar_manager.bar_needs_update = true;
  if (bar->peek_progress < BAR_EXPAND_PROGRESS_MAX) bar_animate_peek(bar);

  uint64_t token = ++bar->peek_token;
  double hold = BAR_PEEK_DURATION / 60.0
                + (bar->peek_suggestion ? claude_suggest_duration()
                                        : g_bar_manager.notification_peek_hold);
  dispatch_after(dispatch_time(DISPATCH_TIME_NOW, hold * NSEC_PER_SEC),
                 dispatch_get_main_queue(),
                 ^{
    if (!bar_is_alive(bar) || token != bar->peek_token || !bar->peeking)
      return;
    // A suggestion waited for this peek: it takes over right away.
    if (bar->has_peek_next) {
      struct notification_item next = bar->peek_next;
      bar->has_peek_next = false;
      if (claude_suggestion_active(&next)) {
        bar_peek(bar, &next);
        windows_unfreeze();
        return;
      }
    }
    bar->peeking = false;
    bar_animate_peek(bar);
    windows_unfreeze();
  });
}

// Claude suggested a next step: it peeks in the closed island of the active
// display (after a notification still peeking there). In an open island the
// Alerts card shows it.
static void bar_island_claude_suggested(const struct notification_item* item) {
  for (int i = 0; i < g_bar_manager.bar_count; i++) {
    struct bar* bar = g_bar_manager.bars[i];
    if (!bar || bar->expanded || bar->expand_progress > 0) continue;
    if (!bar_is_horizontal() || !bar->shown || bar->hidden
        || bar->adid != g_bar_manager.active_adid        ) {
      continue;
    }
    if (bar->peeking && !bar->peek_suggestion) {
      bar->peek_next = *item;
      bar->has_peek_next = true;
    } else {
      bar_peek(bar, item);
    }
  }
  g_bar_manager.bar_needs_update = true;
  bar_manager_refresh(&g_bar_manager, false);
  windows_unfreeze();
}

// A click on a suggestion peek's Yes / No answers it and ends the peek.
static bool bar_peek_click(struct bar* bar, CGPoint point) {
  if (bar->expanded || !bar->peeking || !bar->peek_suggestion
      || bar->peek_progress < BAR_EXPAND_PROGRESS_MAX / 2   ) {
    return false;
  }
  CGRect choices, text, yes, no;
  bar_get_peek_rects(bar, &choices, &text);
  island_split_choices(choices, &yes, &no);
  CGPoint local = bar_get_local_point(bar, point);
  bool hit_yes = CGRectContainsPoint(CGRectInset(yes, -3, -3), local);
  bool hit_no = CGRectContainsPoint(CGRectInset(no, -3, -3), local);
  if (!hit_yes && !hit_no) return false;

  claude_answer(&bar->peek_item, hit_yes);
  bar->has_peek_next = false;
  bar->peeking = false;
  bar->peek_token++;
  bar_animate_peek(bar);
  return true;
}

// Captured notifications changed. A new one (newer than the last peeked)
// peeks in the closed island on the active display; open islands just
// update their list.
static CFAbsoluteTime g_peek_time = 0;

static void bar_island_notifications_changed(void) {
  const struct notification_list* list = notifications_get();
  // Only what just arrived: not the stored history read at start.
  CFAbsoluteTime now = CFAbsoluteTimeGetCurrent();
  bool arrived = list->count > 0 && list->items[0].time > g_peek_time
                 && list->items[0].time > now - BAR_PEEK_RECENT;
  if (list->count > 0 && list->items[0].time > g_peek_time)
    g_peek_time = list->items[0].time;

  for (int i = 0; i < g_bar_manager.bar_count; i++) {
    struct bar* bar = g_bar_manager.bars[i];
    if (!bar) continue;
    if (bar->expanded || bar->expand_progress > 0) {
      // New cards slide in, closed ones out (animated only where the Alerts
      // tab is showing).
      bool visible = bar->expanded
                     && bar->island.service == ISLAND_SERVICE_ALERTS;
      if (island_sync_cards(&bar->island, visible))
        bar_animate_island_cards(bar);
      island_clamp_scroll(&bar->island, bar_get_island_panel(bar));
    } else if (arrived && g_bar_manager.notification_peek_hold > 0
               && bar_is_horizontal() && bar->shown && !bar->hidden
               && bar->adid == g_bar_manager.active_adid               ) {
      bar_peek(bar, &list->items[0]);
    }
  }
  g_bar_manager.bar_needs_update = true;
  bar_manager_refresh(&g_bar_manager, false);
  windows_unfreeze();
}

// Started with the first bar and never stopped, see BAR_STATS_INTERVAL.
// Notification capture starts with it: banners only show for a few seconds,
// so it has to run while the island is closed too.
// Claude's sessions, reachability or models changed.
static void bar_island_claude_changed(void) {
  for (int i = 0; i < g_bar_manager.bar_count; i++) {
    struct bar* bar = g_bar_manager.bars[i];
    if (bar && bar->expanded && bar->island.service == ISLAND_SERVICE_CLAUDE)
      island_clamp_scroll(&bar->island, bar_get_island_panel(bar));
  }
  if (!bar_any_expanded()) return;
  g_bar_manager.bar_needs_update = true;
  bar_manager_refresh(&g_bar_manager, false);
  windows_unfreeze();
}

static void bar_start_stats_history() {
  if (g_stats_timer) return;
  notifications_begin(bar_island_notifications_changed);
  claude_set_callback(bar_island_claude_changed);
  claude_set_suggest_callback(bar_island_claude_suggested);

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

// ------------------------------------------------------------ switches
// A clicked switch flips right away (optimistic) and keeps that until the
// real state catches up, or BAR_TOGGLE_PENDING seconds passed.
static int g_toggle_pending[2] = { -1, -1 };
static CFAbsoluteTime g_toggle_pending_since[2];

static bool bar_toggle_actual(int column) {
  return column == ISLAND_TOGGLE_WIFI ? system_info_get()->wifi_on
                                      : connections_get()->bluetooth_on;
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

  if (column == ISLAND_TOGGLE_WIFI) system_info_set_wifi_power(on);
  else connections_set_bluetooth_power(on);
  bar_sync_toggles(bar);

  // If the change never happens, nothing else redraws: look again once the
  // optimistic state expired, so the switch slides back.
  dispatch_after(dispatch_time(DISPATCH_TIME_NOW,
                               (BAR_TOGGLE_PENDING + 0.1) * NSEC_PER_SEC),
                 dispatch_get_main_queue(),
                 ^{ bar_island_connections_changed(); });
}

// Bluetooth state or a connect result changed.
static void bar_island_connections_changed(void) {
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

  // Bluetooth only while its tab is showing.
  bool connections = false;
  for (int i = 0; i < g_bar_manager.bar_count; i++) {
    struct bar* bar = g_bar_manager.bars[i];
    if (bar && bar->expanded
        && bar->island.service == ISLAND_SERVICE_BLUETOOTH) {
      connections = true;
    }
  }
  if (connections) connections_start(bar_island_connections_changed);
  else connections_stop();

  // Claude: sessions and reachability only while its tab is showing.
  bool claude = false;
  for (int i = 0; i < g_bar_manager.bar_count; i++) {
    struct bar* bar = g_bar_manager.bars[i];
    if (bar && bar->expanded && bar->island.service == ISLAND_SERVICE_CLAUDE)
      claude = true;
  }
  claude_watch(claude);

  // CPU/RAM are already current (history timer); the battery isn't read
  // while closed, so read it now.
  if (needed) system_stats_sample_battery(&g_island_stats);
}

void bar_toggle_expanded(struct bar* bar) {
  // Clicking a peek opens the island on the notifications.
  if (!bar->expanded && (bar->peeking || bar->peek_progress > 0)) {
    bar_stop_peek(bar);
    bar->island.service = ISLAND_SERVICE_ALERTS;
  }
  bar->expanded = !bar->expanded;
  bar_cancel_island_scroll(bar);
  bar_cancel_island_transition(bar);
  if (bar->expanded) {
    island_open(&bar->island,
                g_bar_manager.default_item.label.font.family);
    bar_cancel_island_cards(bar);
    island_sync_cards(&bar->island, false);
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
  bar_cancel_toggles(bar);
  bar_cancel_peek(bar);
  bar_cancel_island_cards(bar);
  window_close(&bar->window);
  free(bar);
}
