#pragma once
#include "misc/helpers.h"

// Unread notifications for the island's Alerts tab, from two sources, since
// each app chooses where its notifications go (System Settings >
// Notifications > app: Desktop, Notification Center):
//
// - Notification Center's database: what it currently stores (apps with
//   "Notification Center" on). Needs Full Disk Access (`history` false
//   without). Re-read only when the file changed.
// - Banners on screen (apps with "Desktop" on, also ones that are never
//   stored): through Accessibility each is an AXNotificationCenterBanner with
//   a unique identifier, its app in the description and #title / #body
//   texts. Needs Accessibility (`trusted`).
//
// Both are checked every NOTIFICATIONS_SCAN seconds (a banner shows for ~5s,
// so none is missed), also while the island is closed. A notification found
// in both is listed once; one dismissed in Notification Center leaves the
// list. Apps with neither on can't be seen at all.
//
// The list lives in memory only (no history across restarts), at most
// NOTIFICATIONS_PER_APP per app (the newest). Opening a notification or "read
// all" removes it; it isn't captured again.
//
// Accessibility is asked for once; Full Disk Access has to be granted in
// Privacy & Security > Full Disk Access (add the sketchybar binary).

#define NOTIFICATIONS_MAX   50
#define NOTIFICATIONS_PER_APP 5
#define NOTIFICATIONS_SCAN  2.0
#define NOTIFICATIONS_TEXT  256

struct notification_item {
  char id[64];
  char app[64];
  char title[128];
  char body[NOTIFICATIONS_TEXT];
  CFAbsoluteTime time;  // delivered (stored ones) or captured (banners)
};

struct notification_list {
  bool trusted;   // Accessibility: banners
  bool history;   // Full Disk Access: Notification Center's database
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
// Marks it read without opening anything (the card's close button).
void notifications_dismiss(int index);
void notifications_read_all(void);
