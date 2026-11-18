#pragma once
#include "misc/helpers.h"
#include "stats.h"
#include "now_playing.h"
#include "audio_output.h"
#include "system_info.h"
#include "connections.h"
#include "desktops.h"

// Content drawn inside the expanded notch island: a services column on the
// left, a divider, and the selected service's panel on the right (for Alerts:
// a scrollable list of glass notification cards with a scroll indicator).
//
// Everything here only exists while some island is open. island_open() loads
// fonts and data, island_release() frees them again once all islands have
// finished collapsing, so a collapsed island costs nothing.

enum island_service {
  ISLAND_SERVICE_ALERTS,
  ISLAND_SERVICE_MUSIC,
  ISLAND_SERVICE_SYSTEM,
  ISLAND_SERVICE_BLUETOOTH,
  ISLAND_SERVICE_SCREENS,
  ISLAND_SERVICE_COUNT
};

// Per-bar UI state. Kept across open/close, except the scroll position.
// Scroll input only moves `scroll_target`; the bar animates `scroll_offset`
// (what is drawn) towards it on the display link. The scrolling page (the
// Alerts list or the Bluetooth device list) uses it.
//
// Services behave like stacked pages: switching runs `transition` 0 -> 1
// while the page of `previous_service` slides out and the new one slides in
// (upwards when the new service is below the old one, downwards otherwise).
struct island {
  int service;
  CGFloat scroll_offset;
  CGFloat scroll_target;

  int previous_service;
  CGFloat previous_scroll_offset;
  double transition;

  // A drag that started on the Music tab's volume slider (or the System
  // tab's brightness slider) is in progress.
  bool dragging_volume;
  bool dragging_brightness;

  // On/off switches (ISLAND_TOGGLE_*: Wi-Fi in the System tab, Bluetooth in
  // its tab): positions 0 off .. 1 on, animated by the bar. `toggles_ready`
  // false: not synced yet, jump instead of animating.
  double toggle_progress[2];
  bool toggles_ready;
};

enum { ISLAND_TOGGLE_WIFI, ISLAND_TOGGLE_BLUETOOTH };

// Live data shown in the island, owned by the bar (only valid while open).
struct island_data {
  const struct system_stats* stats;
  const struct system_stats_history* history;
  const struct now_playing* media;
  const struct audio_output* audio;
  const struct system_info* info;
  const struct connections* connections;

  // Screens tab: the display's desktops, and why clicking a desktop can't
  // switch to it (ISLAND_SCREENS_*).
  const struct desktops* desktops;
  int desktops_problem;

  // System tab: the display's brightness (0..1), if it can be changed.
  bool has_brightness;
  float brightness;
};

// The Screens tab shows this many desktops, each with a purpose.
#define ISLAND_SCREEN_COUNT 6

enum island_screens_problem {
  ISLAND_SCREENS_OK,
  ISLAND_SCREENS_NO_SHORTCUTS,  // "Switch to Desktop N" shortcuts are off
  ISLAND_SCREENS_NO_ACCESS      // no Accessibility access to press them
};

// What an input event on the island content asks the bar to do.
enum island_action_type {
  ISLAND_ACTION_NONE,
  ISLAND_ACTION_PREVIOUS,
  ISLAND_ACTION_PLAY_PAUSE,
  ISLAND_ACTION_NEXT,
  ISLAND_ACTION_TOGGLE_MUTE,
  ISLAND_ACTION_SET_VOLUME,
  ISLAND_ACTION_WIFI_POWER,
  ISLAND_ACTION_WIFI_SETTINGS,
  ISLAND_ACTION_BLUETOOTH_POWER,
  ISLAND_ACTION_BLUETOOTH_SETTINGS,
  ISLAND_ACTION_BLUETOOTH_SELECT,
  ISLAND_ACTION_SCREEN_SELECT,
  ISLAND_ACTION_SET_BRIGHTNESS
};

struct island_action {
  enum island_action_type type;
  float value;   // SET_VOLUME / SET_BRIGHTNESS: 0..1
  int index;     // BLUETOOTH_SELECT: index into the device list;
                 // SCREEN_SELECT: the desktop
};

// `family` is the text font family (the bar's default font); NULL uses the
// built-in default.
void island_open(struct island* island, const char* family);
void island_release(void);

// All rects and points are in window-local coordinates (y-up). `panel` is the
// area below the notch band, `clip` the currently visible island shape, and
// `progress` the expand progress in [0, 1] (used to fade the content in).
void island_draw(struct island* island,
                 CGContextRef context,
                 CGRect panel,
                 CGRect clip,
                 CGFloat corner_radius,
                 const struct island_data* data,
                 double progress                    );

// Draws into the notch band beside the camera housing: today's date on the
// left, battery/CPU/RAM on the right.
void island_draw_band(struct island* island,
                      CGContextRef context,
                      CGRect left,
                      CGRect right,
                      const struct island_data* data,
                      double progress                  );

// Return true if the event was consumed by the island content. Clicks and
// drags report what to do in `action` (ISLAND_ACTION_NONE if nothing).
// island_handle_scroll only updates scroll_target (clamped to the content).
bool island_handle_click(struct island* island,
                         CGRect panel,
                         CGPoint point,
                         struct island_action* action);
bool island_handle_drag(struct island* island,
                        CGRect panel,
                        CGPoint point,
                        struct island_action* action);
bool island_handle_scroll(struct island* island,
                          CGRect panel,
                          CGPoint point,
                          CGFloat delta          );
