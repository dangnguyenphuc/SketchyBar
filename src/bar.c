#include "bar.h"
#include "bar_manager.h"
#include "event.h"
#include "display.h"
#include "misc/helpers.h"
#include "window.h"
#include "mouse.h"

extern int workspace_display_notch_height(uint32_t did);

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

// Size of the visible notch shape for a given expand progress.
static CGSize bar_get_shape_size(struct bar* bar, int progress) {
  double t = (double)progress / BAR_EXPAND_PROGRESS_MAX;
  if (t < 0.0) t = 0.0;
  if (t > 1.0) t = 1.0;

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

// Area of the expanded island below the notch band (window-local, y-up).
static CGRect bar_get_island_panel(struct bar* bar) {
  CGSize size = bar->window.frame.size;
  double band = bar_collapsed_height(bar);
  return (CGRect){{ 0, g_bar_manager.position == POSITION_BOTTOM ? band : 0 },
                  { size.width, size.height - band                          }};
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
  if (!island_handle_click(&bar->island,
                           bar_get_island_panel(bar),
                           bar_get_local_point(bar, point))) {
    return false;
  }

  if (bar->island.service != service) bar_animate_island_transition(bar);
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
      island_draw(&bar->island,
                  bar->window.surface->context,
                  bar_get_island_panel(bar),
                  bar_get_drawn_shape(bar),
                  g_bar_manager.background.corner_radius,
                  (double)bar->expand_progress / BAR_EXPAND_PROGRESS_MAX);
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
    CGSize size = bar_get_shape_size(bar, bar_uses_expanded_window(bar)
                                          ? BAR_EXPAND_PROGRESS_MAX
                                          : 0                       );

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
    if (bar->mouse_over) bar->island_activity = now;
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

void bar_toggle_expanded(struct bar* bar) {
  bar->expanded = !bar->expanded;
  bar_cancel_island_scroll(bar);
  bar_cancel_island_transition(bar);
  if (bar->expanded) {
    island_open(&bar->island,
                g_bar_manager.default_item.label.font.family);
    bar->island_activity = CFAbsoluteTimeGetCurrent();
  }
  bar_island_schedule_idle();

  // Clicks outside of our windows are only observed while an island is open.
  mouse_watch_outside_clicks(bar_any_expanded());

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
  bar->dsid = display_space_id(did);
  bar->sid = mission_control_index(bar->dsid);
  bar->shown = SLSSpaceGetType(g_connection, bar->dsid) != 4;
  g_bar_manager.bar_needs_update = true;
  bar_create_window(bar);
  return bar;
}

void bar_destroy(struct bar *bar) {
  // Don't leave a running expand animation pointing at freed memory.
  animator_cancel_locked(&g_bar_manager.animator,
                         bar,
                         (animator_function*)bar_set_expand_progress);
  bar_cancel_island_scroll(bar);
  bar_cancel_island_transition(bar);
  window_close(&bar->window);
  free(bar);
}
