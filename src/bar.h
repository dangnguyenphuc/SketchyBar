#pragma once
#include "bar_item.h"
#include "misc/helpers.h"
#include "window.h"
#include "island.h"

// Notch "island" geometry (points). The visible shape interpolates between
// the collapsed and expanded size while expand_progress goes 0 ->
// BAR_EXPAND_PROGRESS_MAX. The collapsed height is the bar height (or
// notch_display_height on the built-in display, if set).
#define BAR_NOTCH_COLLAPSED_WIDTH 218
#define BAR_NOTCH_EXPANDED_WIDTH  291
#define BAR_NOTCH_EXPANDED_HEIGHT 183
#define BAR_EXPAND_PROGRESS_MAX   1000
#define BAR_EXPAND_DURATION       30

// Island scrolling. Trackpads (continuous pixel deltas, system momentum) are
// applied immediately. Mouse wheels (line deltas) glide: every frame the list
// covers 1 - e^(-dt/TAU) of the remaining distance (TAU in seconds; ~3*TAU
// until it settles). The glide is driven for at most GLIDE_FRAMES (60Hz).
#define BAR_SCROLL_LINE_HEIGHT    32
#define BAR_SCROLL_WHEEL_TAU      0.045
#define BAR_SCROLL_GLIDE_FRAMES   40

// Slide between service pages (60Hz frames, ease-out).
#define BAR_PAGE_DURATION         22

// An open island closes after this many seconds without a click, scroll or
// hover. It never closes while the pointer is over it.
#define BAR_ISLAND_IDLE_TIMEOUT   60.0

struct bar {
  bool shown;
  bool hidden;
  bool mouse_over;
  bool expanded;
  int expand_progress;
  uint32_t sid;
  uint32_t dsid;
  uint32_t did;
  uint32_t adid;

  struct window window;
  struct island island;
  uint64_t scroll_glide_time;
  CFAbsoluteTime island_activity;
};

struct bar *bar_create(uint32_t did);
void bar_close_window(struct bar* bar);
void bar_destroy(struct bar* bar);
void bar_set_hidden(struct bar* bar, bool hidden);
void bar_calculate_bounds(struct bar* bar);
void bar_resize(struct bar* bar);
void bar_draw(struct bar* bar, bool forced);
void bar_order_item_windows(struct bar* bar);

bool bar_draws_item(struct bar* bar, struct bar_item* bar_item);
bool bar_set_expand_progress(void* target, int progress);
void bar_toggle_expanded(struct bar* bar);
bool bar_island_click(struct bar* bar, CGPoint point);
bool bar_island_scroll(struct bar* bar,
                       CGPoint point,
                       CGFloat delta,
                       bool continuous);
bool bar_contains_point(struct bar* bar, CGPoint point);
void bar_island_touch(struct bar* bar);

void bar_change_space(struct bar* bar, uint64_t dsid);
