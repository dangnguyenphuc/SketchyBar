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
// Wi-Fi / Bluetooth switches: slide duration (60Hz frames), and how long a
// clicked switch shows the new state before falling back to the real one
// (when the change didn't happen).
#define BAR_TOGGLE_DURATION       14
#define BAR_TOGGLE_PENDING        5.0

// Peek timing: grows in BAR_PEEK_DURATION frames (60Hz), holds for
// notification_peek_duration seconds (default BAR_PEEK_HOLD; 0: no peek),
// shrinks in BAR_PEEK_DURATION frames: 3s in total by default.
// Layout: padding around the app icon and the text beside the camera.
// Alerts card animations are driven for at most this many frames (60Hz).
#define BAR_CARDS_FRAMES          40

#define BAR_PEEK_DURATION         15
#define BAR_PEEK_HOLD             3
#define BAR_PEEK_PADDING          10
#define BAR_PEEK_ICON             22
// Only notifications at most this many seconds old peek.
#define BAR_PEEK_RECENT           15.0

// System Settings pages behind the settings buttons.
#define BAR_WIFI_SETTINGS_URL \
  "x-apple.systempreferences:com.apple.wifi-settings-extension"
#define BAR_BLUETOOTH_SETTINGS_URL \
  "x-apple.systempreferences:com.apple.BluetoothSettings"
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

// An open island closes after island_idle_timeout seconds (default below;
// 0: never) without a click, scroll or hover. It never closes while the
// pointer is over it.
#define BAR_ISLAND_IDLE_TIMEOUT   60.0

// CPU/RAM are sampled every BAR_STATS_INTERVAL seconds from startup on, open
// or not, so the System tab's graphs always cover the last
// SYSTEM_STATS_HISTORY samples. While all islands are closed that is all the
// timer does (no battery, no redraw).
#define BAR_STATS_INTERVAL        2.0
#define BAR_STATS_TOLERANCE       0.5

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
  // Camera housing width on this display (0: none, use notch_width).
  uint32_t notch_width;

  struct window window;
  struct island island;
  // Screens tab: this display's desktops, read while the tab is shown.
  struct desktops desktops;
  int desktops_problem;
  // System tab: this display's brightness, read while the tab is shown.
  bool has_brightness;
  float brightness;

  // Peek: a new notification while the island is closed. The island keeps
  // its collapsed height and widens beside the camera (`peek_left` points for
  // the app icon, `peek_right` for the text); `peek_progress` animates it.
  bool peeking;
  int peek_progress;
  CGFloat peek_left;
  CGFloat peek_right;
  uint64_t peek_token;
  struct notification_item peek_item;
  char peek_text[256];
  // The peek shows Claude's suggested next step (Yes / No left of the
  // camera, the suggestion right of it) for claude_suggest_duration seconds.
  // One that arrives while another notification peeks waits in
  // `peek_next` until that one's hold ends.
  bool peek_suggestion;
  bool has_peek_next;
  struct notification_item peek_next;
  uint64_t scroll_glide_time;
  uint64_t cards_time;
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
bool bar_island_drag(struct bar* bar, CGPoint point);
void bar_island_spaces_changed(struct bar* bar);
bool bar_island_click(struct bar* bar, CGPoint point);
bool bar_island_scroll(struct bar* bar,
                       CGPoint point,
                       CGFloat delta,
                       bool continuous);
bool bar_contains_point(struct bar* bar, CGPoint point);
void bar_island_touch(struct bar* bar);
// An island setting changed (sketchybarrc): applies it to open islands.
void bar_island_settings_changed(void);

void bar_change_space(struct bar* bar, uint64_t dsid);
