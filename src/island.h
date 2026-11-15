#pragma once
#include "misc/helpers.h"

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
  ISLAND_SERVICE_CALENDAR,
  ISLAND_SERVICE_COUNT
};

// Per-bar UI state. Kept across open/close, except the scroll position.
// Scroll input only moves `scroll_target`; the bar animates `scroll_offset`
// (what is drawn) towards it on the display link.
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
};

void island_open(struct island* island);
void island_release(void);

// All rects and points are in window-local coordinates (y-up). `panel` is the
// area below the notch band, `clip` the currently visible island shape, and
// `progress` the expand progress in [0, 1] (used to fade the content in).
void island_draw(struct island* island,
                 CGContextRef context,
                 CGRect panel,
                 CGRect clip,
                 CGFloat corner_radius,
                 double progress                    );

// Return true if the event was consumed by the island content.
// island_handle_scroll only updates scroll_target (clamped to the content).
bool island_handle_click(struct island* island, CGRect panel, CGPoint point);
bool island_handle_scroll(struct island* island,
                          CGRect panel,
                          CGPoint point,
                          CGFloat delta          );
