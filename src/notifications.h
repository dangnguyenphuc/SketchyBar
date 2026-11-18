#pragma once
#include "misc/helpers.h"

// Unread notifications for the island's Alerts tab, captured from what
// Notification Center shows on screen.
//
// Not every notification ends up in Notification Center's database (apps
// remove theirs once read elsewhere, iPhone-mirrored ones live elsewhere),
// but every one is drawn by Notification Center: as a banner, and in its list
// when opened. Through Accessibility each one is an AXNotificationCenterBanner
// element with a unique identifier, its app in the description and #title /
// #body texts. Notification Center's windows are scanned every
// NOTIFICATIONS_SCAN seconds (a banner shows for ~5s, so none is missed),
// also while the island is closed. No real-time observer: a scan is a few
// cheap Accessibility reads.
//
// The list lives in memory only (no history across restarts), at most
// NOTIFICATIONS_PER_APP per app (the newest). Opening a notification or "read
// all" removes it; it isn't captured again.
//
// Needs Accessibility access (Privacy & Security > Accessibility), asked for
// once; until then `trusted` is false and nothing is captured.

#define NOTIFICATIONS_MAX   50
#define NOTIFICATIONS_PER_APP 5
#define NOTIFICATIONS_SCAN  2.0
#define NOTIFICATIONS_TEXT  256

struct notification_item {
  char id[64];
  char app[64];
  char title[128];
  char body[NOTIFICATIONS_TEXT];
  CFAbsoluteTime time;  // when it was captured
};

struct notification_list {
  bool trusted;
  int count;
  struct notification_item items[NOTIFICATIONS_MAX];  // newest first
};

// Called on the main thread whenever the list changed.
typedef void notifications_callback(void);

// Starts capturing (for the life of the process).
void notifications_begin(notifications_callback* on_change);
const struct notification_list* notifications_get(void);

// Opens the notification's app and marks it read (removes it).
void notifications_open(int index);
void notifications_read_all(void);
